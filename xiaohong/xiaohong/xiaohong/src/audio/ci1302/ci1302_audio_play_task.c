/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdint.h>
#include <string.h>
#include "board_config.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "audio_config.h"
#include "task_entry.h"
#include "settings.h"
#include "net_wifi_config.h"

#include "ci1302_data_process.h"
#include "ci1302_tts_downlink.h"

extern osMessageQueueId_t g_audx_event_qid;

#define CI1302_UART_WRITE_RETRY_MAX      (8U)
#define CI1302_UART_WRITE_TIMEOUT_MS     (5U)
#define CI1302_UART_WRITE_RETRY_DELAY_US (500U)

/* 单帧 0x020B 载荷上限；与 ci1302_tts_downlink.h CI1302_TTS_CHUNK_MAX_BYTES 一致 */
#define CI1302_UART_PAYLOAD_MAX   CI1302_TTS_CHUNK_MAX_BYTES    // 4096
uint8_t* ci1302_audio_block = NULL;
uint16_t ci1302_audio_block_len = 0;

// 优化思路：直接把ci1302_audio_block的发音频数据下发，减少数据的拷贝
static void process_send_cmd_payload(uint16_t cmd, const uint8_t *payload, uint16_t len)
{
    if ((len > 0) && (payload == NULL)) {
        log_error("[Aud_Play] process_send_cmd_payload: len[%d] & payload[NULL] is invalid\r\n", len);
        return;
    }
    if (len > CI1302_UART_PAYLOAD_MAX) {  // 音频数据长度超过4096字节，则截取前4096字节
        len = CI1302_UART_PAYLOAD_MAX;
    }
    uint8_t tx_buff[16] = {
        0xA5, 0xA5, 0x5A, 0x5A,  // head[4]
        0x00, 0x00, 0x00, 0x00,  // checksum[lo,hi] + cmd_type[lo,hi]
        0x00, 0x00, 0x00, 0x00,  // data_len[lo,hi] + version[lo,hi]
        0x78, 0x56, 0x34, 0x12   // tail[4]
    };
    tx_buff[6] = (uint8_t)(cmd & 0xFF);
    tx_buff[7] = (uint8_t)((cmd >> 8) & 0xFF);
    if (len > 0) {
        tx_buff[8] = (uint8_t)(len & 0xFF);
        tx_buff[9] = (uint8_t)((len >> 8) & 0xFF);
    }

    // 1. 先下发帧头16字节
    uint16_t written = 0;
    uint16_t off = 0;
    uint8_t retry = 0;
    while (off < 16U) {
        written = uapi_uart_write(UART_BUS_ID, tx_buff + off, (uint16_t)(16U - off), CI1302_UART_WRITE_TIMEOUT_MS);
        if (written == 0U) {
            if (++retry >= CI1302_UART_WRITE_RETRY_MAX) {
                break;
            }
            usleep(CI1302_UART_WRITE_RETRY_DELAY_US);
            continue;
        }
        off = (uint16_t)(off + written);
        retry = 0U;
    }
    if (off != 16U) {
        log_error("[Aud_Play] MCU ==>> CI1302 ==>> NG: cmd[0x%04X] NG [%d]->[%d] => abort\r\n", cmd, 16, written);
    } else {
    // 2. 如有必要，再下发payload：命令参数或音频数据
        if (len > 0) {
            uint16_t pay_off = 0;
            retry = 0U;
            while (pay_off < len) {
                written = uapi_uart_write(UART_BUS_ID, payload + pay_off, (uint16_t)(len - pay_off),
                                          CI1302_UART_WRITE_TIMEOUT_MS);
                if (written == 0U) {
                    if (++retry >= CI1302_UART_WRITE_RETRY_MAX) {
                        break;
                    }
                    usleep(CI1302_UART_WRITE_RETRY_DELAY_US);
                    continue;
                }
                pay_off = (uint16_t)(pay_off + written);
                retry = 0U;
            }
        }
    }
}

void process_send_cmd(uint16_t cmd, uint8_t *data, uint16_t len)
{
    process_send_cmd_payload(cmd, data, len);
}

void AudioPlayTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint8_t msg_recv = 0;
    uint8_t args[8] = {0};

#if 1  // 开机联网成功后，启动语音芯片线程时，对语音芯片进行初始化设置
    // 设置持续唤醒时间为10分钟，超过该时间，语音芯片强制退出唤醒，带参数2字节(默认 15S 时间太短)
    uint16_t wakeup_time = 600;

    args[0] = wakeup_time&0xFF;
    args[1] = (wakeup_time>>8)&0xFF;
    process_send_cmd(0x0113, args, 2);

    // 开机对齐音量
    if (get_volume() == 0) {
        args[0] = 1;  // Mute
        process_send_cmd(0x0119, args, 1);
    } else {
        args[0] = 0;  // unMute
        process_send_cmd(0x0119, args, 1);

        args[0] = 1 + (get_volume() - VOL_MIN)/VOL_STEP;
        process_send_cmd(0x0117, args, 1);
    }

    // 开机唤醒: 1: 唤醒+播放唤醒语，0-仅唤醒，不播放唤醒语
    args[0] = 1;
    process_send_cmd(0x0114, args, 1);

    // 开机对齐 interrupt mode
    args[0] = get_voice_interrupt_enabled();
    process_send_cmd(0x0116, args, 1);

    // 设置多长时间的静音产生 vad end，带参数 1 字节(默认 20)，
    // 允许语音停顿的时间；6:约300ms；15:约400ms；20:约500ms；50:约1S；80:约1.5S；120:约2S
    args[0] = 50;
    process_send_cmd(0x0110, args, 1);
#endif

    while (1) {
        // opus 音频数据，每帧 40ms，所以 block 多长时间合适？
        ret = osMessageQueueGet(g_audx_event_qid, (void *)&msg_recv, NULL, 5);  //block 20ms
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }
        if (msg_recv != eAud_SendAudioData) {
            log_debug("[Aud_Play] osMessageQueueGet: msg[%d]\r\n", msg_recv);
        }

        // 这里处理其它任务发过来的需要CI1302处理的：
        // [1]命令（如按键唤醒、音量控制）
        // [2]播放的本地语音（如欢迎语、提示语）
        // [3] Agent TTS：Opus 已在 ws 线程解码为 PCM 入 ci1302_tts；CI1302 发 0x020A 后此处取 PCM 发 0x020B。
        switch (msg_recv) {
            case eAud_WakeUp:  // 按键唤醒
                args[0] = 1;   // 1: 唤醒+播放唤醒语，0-仅唤醒，不播放唤醒语
                process_send_cmd(0x0114, args, 1);

                // 每次唤醒都重新设置一次唤醒持续时间
                args[0] = wakeup_time&0xFF;
                args[1] = (wakeup_time>>8)&0xFF;
                process_send_cmd(0x0113, args, 2);
                break;
            case eAud_StartPlay:
                log_debug("[Aud_Play] MCU ==>> CI1302 ==>>   : eAud_StartPlay\r\n");
                process_send_cmd(0x0201, NULL, 0);
                break;
            case eAud_SendAudioData: {
                bool all_drained = false;
                /* CI1302 请求下一帧下行音频数据[4K]：AudioPlayTask 从 ci1302_tts 取已解码的 PCM 发 0x020B */
                if (ci1302_tts_take_next_tx_block(&ci1302_audio_block, &ci1302_audio_block_len,
                                                  CI1302_UART_PAYLOAD_MAX, &all_drained)) {
                    process_send_cmd_payload(0x020B, ci1302_audio_block, ci1302_audio_block_len);
                }

                /* 勿在「任意一次 all_drained」时 EndPlay：流式 TTS 两包之间队列会短暂空，只播半句。
                 * 仅当 tts/stop 后仍待播完时，由 ci1302_tts_set_pending_endplay_after_drain 配合此处。 */
                if (//g_audx_event_qid != NULL &&
                    ci1302_tts_consume_endplay_after_drain(all_drained)) {
                    log_info("[Aud_Play] ci1302_tts_consume_endplay_after_drain: all_drained[%d] send EndPlay => CI1302\r\n", all_drained);
                    //uint8_t m = eAud_EndPlay;
                    //(void)osMessageQueuePut(g_audx_event_qid, &m, 0, 0);
                    // 注意：这里没有break，直接flow到 eAud_EndPlay，向CI1302发送0x020C指令，播放结束
                } else {
                    break;
                }
            }
            case eAud_EndPlay:
                usleep(300*1000);  // 等待300ms，让CI1302完成播放
                log_debug("[Aud_Play] MCU ==>> CI1302 ==>>   : eAud_EndPlay\n");
                process_send_cmd(0x020C, NULL, 0);
                process_send_cmd(0x0204, NULL, 0);
                ci1302_tts_reset();
                break;
            case eAud_InterruptMode:
                args[0] = get_voice_interrupt_enabled();
                process_send_cmd(0x0116, args, 1);
                //切换中断模式后，马上发一次唤醒命令，让CI1302实现切换
                args[0] = 1;   // 1: 唤醒+播放唤醒语，0-仅唤醒，不播放唤醒语
                process_send_cmd(0x0114, args, 1);
                break;
            case eAud_VolInit:
            case eAud_VolUp:
            case eAud_VolDw:
                if (get_volume() == 0) {
                    args[0] = 1;  // Mute
                    process_send_cmd(0x0119, args, 1);
                } else {
                    args[0] = 0;  // unMute
                    process_send_cmd(0x0119, args, 1);

                    args[0] = 1 + (get_volume() - VOL_MIN)/VOL_STEP;
                    process_send_cmd(0x0117, args, 1);
                }
                break;
            default:
                break;
        }
    }
}

uint32_t AudioPlayTaskEntry(void)
{
    task_config_t task_conf = {
        "Aud_Play",        // task name
        1024 * 8,          // stack_size（大块缓冲已放 static，留余量给调用链/日志）
        24,                // priority
        (void*)AudioPlayTask  // func
    };

    return create_task(&task_conf, NULL);
}
// SYS_RUN(AudioPlayTaskEntry);
// APP_FEATURE_INIT(AudioPlayTaskEntry);