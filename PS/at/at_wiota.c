/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date                 Author                Notes
 * 20201-8-17     ucchip-wz          v0.00
 */
#include <rtthread.h>
#ifdef RT_USING_AT
#ifdef AT_USING_SERVER
#ifdef UC8288_MODULE
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "uc_wiota_api.h"
#include "uc_wiota_static.h"
#include "at.h"
#include "ati_prs.h"
#include "uc_string_lib.h"
#include "uc_adda.h"
#include "uc_uart.h"
//#include "uc_boot_download.h"
#include "at_wiota.h"
#include "at_wiota_gpio_report.h"

const u16_t symLen_mcs_byte[4][8] = {{7, 9, 52, 66, 80, 0, 0, 0},
                                     {7, 15, 22, 52, 108, 157, 192, 0},
                                     {7, 15, 31, 42, 73, 136, 255, 297},
                                     {7, 15, 31, 63, 108, 220, 451, 619}};

enum at_wiota_lpm
{
    AT_WIOTA_SLEEP = 0,
    AT_WIOTA_PAGING_TX, // 1, send tx task, system is noral
    AT_WIOTA_PAGING_RX, // 2, enter paging mode(system is sleep), only phy is working
    AT_WIOTA_GATING,    // 3
    AT_WIOTA_CLOCK,     // 4
    AT_WIOTA_LPM,       // 5
    AT_WIOTA_FREQ_DIV,  // 6
    AT_WIOTA_VOL_MODE,  // 7
    AT_WIOTA_EX_WK,     // 8
    AT_WIOTA_LPM_MAX,
};

enum at_wiota_log
{
    AT_LOG_CLOSE = 0,
    AT_LOG_OPEN,
    AT_LOG_UART0,
    AT_LOG_UART1,
    AT_LOG_SPI_CLOSE,
    AT_LOG_SPI_OPEN,
};

#define ADC_DEV_NAME "adc"
#define WIOTA_TRANS_END_STRING "EOF"

#define WIOTA_SCAN_FREQ_TIMEOUT 120000
#define WIOTA_SEND_TIMEOUT 60000
#define WIOTA_WAIT_DATA_TIMEOUT 10000
#define WIOTA_TRANS_AUTO_SEND 1000
#define WIOTA_SEND_DATA_MUX_LEN 1024
#define WIOTA_DATA_END 0x1A
#define WIOTA_TRANS_MAX_LEN 310
#define WIOTA_TRANS_END_STRING_MAX 8
#define WIOTA_TRANS_BUFF (WIOTA_TRANS_MAX_LEN + WIOTA_TRANS_END_STRING_MAX + CRC16_LEN + 1)

#define WIOTA_MUST_INIT(state)             \
    if (state != AT_WIOTA_INIT)            \
    {                                      \
        return AT_RESULT_REPETITIVE_FAILE; \
    }

#define WIOTA_MUST_RUN(state)              \
    if (state != AT_WIOTA_RUN)             \
    {                                      \
        return AT_RESULT_REPETITIVE_FAILE; \
    }

#define WIOTA_MUST_ALREADY_INIT(state)                   \
    if (state != AT_WIOTA_INIT && state != AT_WIOTA_RUN) \
    {                                                    \
        return AT_RESULT_REPETITIVE_FAILE;               \
    }

#define WIOTA_CHECK_AUTOMATIC_MANAGER()   \
    if (uc_wiota_get_auto_connect_flag()) \
        return AT_RESULT_REFUSED;

enum at_test_mode_data_type
{
    AT_TEST_MODE_RECVDATA = 0,
    AT_TEST_MODE_QUEUE_EXIT,
};

typedef struct at_test_queue_data
{
    enum at_test_mode_data_type type;
    void *data;
    void *paramenter;
} t_at_test_queue_data;

typedef struct at_test_statistical_data
{
    int type;
    int dev;

    int upcurrentrate;
    int upaverate;
    int upminirate;
    int upmaxrate;

    int downcurrentrate;
    int downavgrate;
    int downminirate;
    int downmaxrate;

    int send_fail;
    int recv_fail;
    int max_mcs;
    int msc;
    int power;
    int rssi;
    int snr;
} t_at_test_statistical_data;

extern dtu_send_t g_dtu_send;
extern at_server_t at_get_server(void);
extern char *parse(char *b, char *f, ...);

static int wiota_state = AT_WIOTA_DEFAULT;

void at_wiota_set_state(int state)
{
    wiota_state = state;
}

int at_wiota_get_state(void)
{
    return wiota_state;
}

static rt_err_t get_char_timeout(rt_tick_t timeout, char *chr)
{
    at_server_t at_server = at_get_server();
    return at_server->get_char(at_server, chr, timeout);
}

static at_result_t at_wiota_version_query(void)
{
    u8_t version[15] = {0};
    u8_t git_info[36] = {0};
    u8_t time[36] = {0};
    u32_t cce_version = 0;

    uc_wiota_get_version(version, git_info, time, &cce_version);

    at_server_printfln("+WIOTAVERSION:%s", version);
    at_server_printfln("+GITINFO:%s", git_info);
    at_server_printfln("+TIME:%s", time);
    at_server_printfln("+CCEVERSION:%x", cce_version);

    return AT_RESULT_OK;
}

static at_result_t at_freq_query(void)
{
    at_server_printfln("+WIOTAFREQ=%d", uc_wiota_get_freq_info());

    return AT_RESULT_OK;
}

static at_result_t at_freq_setup(const char *args)
{
    int freq = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_ALREADY_INIT(wiota_state)

    args = parse((char *)(++args), "d", &freq);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_freq_info(freq);

    return AT_RESULT_OK;
}

static at_result_t at_dcxo_setup(const char *args)
{
    int dcxo = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_ALREADY_INIT(wiota_state)

    args = parse((char *)(++args), "y", &dcxo);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("dcxo=0x%x\n", dcxo);
    uc_wiota_set_dcxo(dcxo);

    return AT_RESULT_OK;
}

static at_result_t at_userid_query(void)
{
    unsigned int id[2] = {0};
    unsigned char len = 0;

    uc_wiota_get_userid(&(id[0]), &len);
    at_server_printfln("+WIOTAUSERID=0x%x", id[0]);

    return AT_RESULT_OK;
}

static at_result_t at_userid_setup(const char *args)
{
    unsigned int userid[2] = {0};

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    args = parse((char *)(++args), "y", &userid[0]);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("userid:%x\n", userid[0]);

    uc_wiota_set_userid(userid, 4);

    return AT_RESULT_OK;
}

static at_result_t at_radio_query(void)
{
    rt_uint32_t temp = 0;
    radio_info_t radio;
    rt_device_t adc_dev;

    if (AT_WIOTA_RUN != wiota_state)
    {
        rt_kprintf("radio state %d\n", wiota_state);
        return AT_RESULT_FAILE;
    }

    adc_dev = rt_device_find(ADC_DEV_NAME);
    if (RT_NULL == adc_dev)
    {
        rt_kprintf("adc find fail\n");
    }
    else
    {
        rt_adc_enable((rt_adc_device_t)adc_dev, ADC_CONFIG_CHANNEL_CHIP_TEMP);
        temp = rt_adc_read((rt_adc_device_t)adc_dev, ADC_CONFIG_CHANNEL_CHIP_TEMP);
    }

    uc_wiota_get_radio_info(&radio);
    //temp,rssi,ber,snr,cur_power,max_pow,cur_mcs,max_mcs
    at_server_printfln("+WIOTARADIO=%d,-%d,%d,%d,%d,%d,%d,%d,%d,%d",
                       temp, radio.rssi, radio.ber, radio.snr, radio.cur_power,
                       radio.min_power, radio.max_power, radio.cur_mcs, radio.max_mcs, radio.frac_offset);

    return AT_RESULT_OK;
}

static at_result_t at_system_config_query(void)
{
    sub_system_config_t config;
    uc_wiota_get_system_config(&config);

    at_server_printfln("+WIOTASYSTEMCONFIG=%d,%d,%d,%d,%d,%d,0x%x,0x%x",
                       config.id_len, config.symbol_length, config.bandwidth, config.pz,
                       config.btvalue, config.spectrum_idx, 0, config.subsystemid);

    return AT_RESULT_OK;
}

static at_result_t at_system_config_setup(const char *args)
{
    sub_system_config_t config;
    unsigned int temp[7];

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_INIT(wiota_state)

    uc_wiota_get_system_config(&config);

    args = parse((char *)(++args), "d,d,d,d,d,d,y,y",
                 &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5],
                 &(temp[6]), &config.subsystemid);

    config.id_len = (unsigned char)temp[0];
    config.symbol_length = (unsigned char)temp[1];
    config.bandwidth = (unsigned char)temp[2];
    config.pz = (unsigned char)temp[3];
    config.btvalue = (unsigned char)temp[4];
    config.spectrum_idx = (unsigned char)temp[5];

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    // default config
    config.pp = 1;

    // rt_kprintf("id_len=%d,symbol_len=%d,dlul=%d,bt=%d,group_num=%d,ap_max_pow=%d,spec_idx=%d,freq_idx=%d,subsystemid=0x%x\n",
    //            config.id_len, config.symbol_length, config.dlul_ratio,
    //            config.btvalue, config.group_number, config.ap_max_pow,
    //            config.spectrum_idx, config.freq_idx, config.subsystemid);

    uc_wiota_set_system_config(&config);

    return AT_RESULT_OK;
}

static at_result_t at_subsys_id_query(void)
{
    at_server_printfln("+WIOTASUBSYSID=0x%x", uc_wiota_get_subsystem_id());
    return AT_RESULT_OK;
}

static at_result_t at_subsys_id_setup(const char *args)
{
    unsigned int subsysid;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    WIOTA_MUST_ALREADY_INIT(wiota_state)

    args = parse((char *)(++args), "y", &subsysid);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_subsystem_id((unsigned int)subsysid);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_init_exec(void)
{
    WIOTA_CHECK_AUTOMATIC_MANAGER();

    if (wiota_state == AT_WIOTA_DEFAULT || wiota_state == AT_WIOTA_EXIT)
    {
        uc_wiota_init();
        wiota_state = AT_WIOTA_INIT;
        return AT_RESULT_OK;
    }

    return AT_RESULT_REPETITIVE_FAILE;
}

// extern u8_t state_get_dcxo_idx(void);
// extern s8_t l1_adc_temperature_read(void);

void wiota_recv_callback(uc_recv_back_p data)
{
    // rt_kprintf("wiota_recv_callback result %d\n", data->result);

    if (data->data != RT_NULL && (UC_OP_SUCC == data->result || UC_OP_PART_SUCC == data->result || UC_OP_CRC_FAIL == data->result))
    {
        if (WIOTA_MODE_OUT_UART == wiota_gpio_mode_get())
        {
            if (g_dtu_send->flag && (!g_dtu_send->at_show))
            {
                at_send_data(data->data, data->data_len);
                rt_free(data->data);
                return;
            }
            if (data->type < UC_RECV_SCAN_FREQ)
            {
                at_server_printf("+WIOTARECV,-%d,%d,%d,%d,%d,", data->rssi, data->snr, data->type, data->result, data->data_len);
                at_send_data(data->data, data->data_len);
                at_server_printfln("");
                rt_kprintf("head data %d %d dfe %u\n", data->head_data[0], data->head_data[1], data->cur_rf_cnt);
            }
            rt_free(data->data);
        }
        else
        {
            wiota_data_insert(data);
        }
    }
}

static at_result_t at_wiota_cfun_setup(const char *args)
{
    int state = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d", &state);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    // rt_kprintf("state = %d\n", state);

    if (1 == state && wiota_state == AT_WIOTA_INIT)
    {
        uc_wiota_run();
        uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_NORAMAL_MSG);
        // uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_STATE_INFO);
        wiota_state = AT_WIOTA_RUN;
    }
    else if (0 == state && wiota_state == AT_WIOTA_RUN)
    {
        uc_wiota_exit();
        wiota_state = AT_WIOTA_EXIT;
    }
    else
        return AT_RESULT_REPETITIVE_FAILE;

    return AT_RESULT_OK;
}

static at_result_t at_wiotasend_setup(const char *args)
{
    int length = 0, timeout = 0;
    unsigned char *sendbuffer = NULL;
    unsigned char *psendbuffer;
    unsigned int userId = 0;
    unsigned int timeout_u = 0;

    WIOTA_MUST_RUN(wiota_state)

    args = parse((char *)(++args), "d,d,y", &timeout, &length, &userId);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    timeout_u = (unsigned int)timeout;

    // rt_kprintf("timeout=%d, length=%d\n", timeout, length);

    if (length > 0)
    {
        sendbuffer = (unsigned char *)rt_malloc(length + CRC16_LEN); // reserve CRC16_LEN for low mac
        if (sendbuffer == NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        psendbuffer = sendbuffer;
        //at_server_printfln("SUCC");
        at_server_printf(">");

        while (length)
        {
            if (get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char *)psendbuffer) != RT_EOK)
            {
                at_server_printfln("SEND FAIL");
                rt_free(sendbuffer);
                return AT_RESULT_NULL;
            }
            length--;
            psendbuffer++;
        }

        if (UC_OP_SUCC == uc_wiota_send_data(userId, sendbuffer, psendbuffer - sendbuffer, NULL, 0, timeout_u > 0 ? timeout_u : WIOTA_SEND_TIMEOUT, RT_NULL))
        {
            rt_free(sendbuffer);
            sendbuffer = NULL;

            at_server_printfln("SEND SUCC");
            // at_server_printfln("+dcxo,%d,temp,%d", state_get_dcxo_idx(),l1_adc_temperature_read());
            return AT_RESULT_OK;
        }
        else
        {
            rt_free(sendbuffer);
            sendbuffer = NULL;
            at_server_printfln("SEND FAIL");
            // at_server_printfln("+dcxo,%d,temp,%d", state_get_dcxo_idx(),l1_adc_temperature_read());
            return AT_RESULT_NULL;
        }
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotatrans_process(u16_t timeout, char *strEnd)
{
    uint8_t *pBuff = RT_NULL;
    int result = 0;
    timeout = (timeout == 0) ? WIOTA_SEND_TIMEOUT : timeout;
    if ((RT_NULL == strEnd) || ('\0' == strEnd[0]))
    {
        strEnd = WIOTA_TRANS_END_STRING;
    }
    uint8_t nLenEnd = strlen(strEnd);
    //    uint8_t nStrEndCount = 0;
    int16_t nSeekRx = 0;
    char nRun = 1;
    //    char nCatchEnd = 0;
    char nSendFlag = 0;

    pBuff = (uint8_t *)rt_malloc(WIOTA_TRANS_BUFF);
    if (pBuff == RT_NULL)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    memset(pBuff, 0, WIOTA_TRANS_BUFF);
    at_server_printfln("\r\nEnter transmission mode >");

    while (nRun)
    {
        get_char_timeout(rt_tick_from_millisecond(-1), (char *)&pBuff[nSeekRx]);
        ++nSeekRx;
        if ((nSeekRx > 2) && ('\n' == pBuff[nSeekRx - 1]) && ('\r' == pBuff[nSeekRx - 2]))
        {
            nSendFlag = 1;
            nSeekRx -= 2;
            if ((nSeekRx >= nLenEnd) && pBuff[nSeekRx - 1] == strEnd[nLenEnd - 1])
            {
                int i = 0;
                for (i = 0; i < nLenEnd; ++i)
                {
                    if (pBuff[nSeekRx - nLenEnd + i] != strEnd[i])
                    {
                        break;
                    }
                }
                if (i >= nLenEnd)
                {
                    nSeekRx -= nLenEnd;
                    nRun = 0;
                }
            }
        }

        if ((nSeekRx > (WIOTA_TRANS_MAX_LEN + nLenEnd + 2)) || (nSendFlag && (nSeekRx > WIOTA_TRANS_MAX_LEN)))
        {
            at_server_printfln("\r\nThe message's length can not over 310 characters.");
            do
            {
                // discard any characters after the end string
                result = get_char_timeout(rt_tick_from_millisecond(200), (char *)&pBuff[0]);
            } while (RT_EOK == result);
            nSendFlag = 0;
            nSeekRx = 0;
            nRun = 1;
            memset(pBuff, 0, WIOTA_TRANS_BUFF);
            continue;
        }

        if (nSendFlag)
        {
            nSeekRx = (nSeekRx > WIOTA_TRANS_MAX_LEN) ? WIOTA_TRANS_MAX_LEN : nSeekRx;
            if (nSeekRx > 0)
            {
                if (UC_OP_SUCC == uc_wiota_send_data(0, pBuff, nSeekRx, NULL, 0, timeout, RT_NULL))
                {
                    at_server_printfln("SEND SUCC");
                }
                else
                {
                    at_server_printfln("SEND FAIL");
                }
            }
            nSeekRx = 0;
            nSendFlag = 0;
            memset(pBuff, 0, WIOTA_TRANS_BUFF);
        }
    }

    do
    {
        // discard any characters after the end string
        result = get_char_timeout(rt_tick_from_millisecond(200), (char *)&pBuff[0]);
    } while (RT_EOK == result);

    at_server_printfln("\r\nLeave transmission mode");
    if (RT_NULL != pBuff)
    {
        rt_free(pBuff);
        pBuff = RT_NULL;
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotatrans_setup(const char *args)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    int timeout = 0;
    char strEnd[WIOTA_TRANS_END_STRING_MAX + 1] = {0};

    args = parse((char *)(++args), "d,s", &timeout, (sl32_t)8, strEnd);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    int i = 0;
    for (i = 0; i < WIOTA_TRANS_END_STRING_MAX; i++)
    {
        if (('\r' == strEnd[i]) || ('\n' == strEnd[i]))
        {
            strEnd[i] = '\0';
            break;
        }
    }

    if (i <= 0)
    {
        strcpy(strEnd, WIOTA_TRANS_END_STRING);
    }
    return at_wiotatrans_process(timeout & 0xFFFF, strEnd);
}

static at_result_t at_wiotatrans_exec(void)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    return at_wiotatrans_process(0, RT_NULL);
}

void dtu_send_process(void)
{
    int16_t nSeekRx = 0;
    uint8_t buff[WIOTA_TRANS_BUFF] = {0};
    rt_err_t result;

    int i = 0;
    for (i = 0; i < WIOTA_TRANS_END_STRING_MAX; i++)
    {
        if (('\0' == g_dtu_send->exit_flag[i]) ||
            ('\r' == g_dtu_send->exit_flag[i]) ||
            ('\n' == g_dtu_send->exit_flag[i]))
        {
            g_dtu_send->exit_flag[i] = '\0';
            break;
        }
    }
    g_dtu_send->flag_len = i;
    if (g_dtu_send->flag_len <= 0)
    {
        strcpy(g_dtu_send->exit_flag, WIOTA_TRANS_END_STRING);
        g_dtu_send->flag_len = strlen(WIOTA_TRANS_END_STRING);
    }
    g_dtu_send->timeout = g_dtu_send->timeout ? g_dtu_send->timeout : 5000;
    g_dtu_send->wait = g_dtu_send->wait ? g_dtu_send->wait : 200;

    while (g_dtu_send->flag)
    {
        result = get_char_timeout(rt_tick_from_millisecond(g_dtu_send->wait), (char *)&buff[nSeekRx]);
        if (RT_EOK == result)
        {
            nSeekRx++;
            if ((nSeekRx >= g_dtu_send->flag_len) && (buff[nSeekRx - 1] == g_dtu_send->exit_flag[g_dtu_send->flag_len - 1]))
            {

                int i = 0;
                for (i = 0; i < g_dtu_send->flag_len; ++i)
                {
                    if (buff[nSeekRx - g_dtu_send->flag_len + i] != g_dtu_send->exit_flag[i])
                    {
                        break;
                    }
                }
                if (i >= g_dtu_send->flag_len)
                {
                    nSeekRx -= g_dtu_send->flag_len;
                    g_dtu_send->flag = 0;
                }
            }
            if (g_dtu_send->flag && (nSeekRx > (WIOTA_TRANS_MAX_LEN + g_dtu_send->flag_len)))
            {
                // too long to send
                result = RT_ETIMEOUT;
            }
        }
        if ((nSeekRx > 0) && ((RT_EOK != result) || (0 == g_dtu_send->flag)))
        {
            // timeout to send
            if (nSeekRx > WIOTA_TRANS_MAX_LEN)
            {
                nSeekRx = WIOTA_TRANS_MAX_LEN;
                do
                {
                    // discard any characters after the end string
                    char ch;
                    result = get_char_timeout(rt_tick_from_millisecond(100), &ch);
                } while (RT_EOK == result);
            }

            if ((AT_WIOTA_RUN == wiota_state) &&
                (UC_OP_SUCC == uc_wiota_send_data(0, buff, nSeekRx, NULL, 0, g_dtu_send->timeout, RT_NULL)))
            {
                if (g_dtu_send->at_show)
                {
                    at_server_printfln("SEND:%4d.", nSeekRx);
                }
            }
            else
            {
                at_server_printfln("SEND FAIL");
            }
            nSeekRx = 0;
            memset(buff, 0, WIOTA_TRANS_BUFF);
        }
    }
    do
    {
        // discard any characters after the end string
        result = get_char_timeout(rt_tick_from_millisecond(100), (char *)&buff[0]);
    } while (RT_EOK == result);
    if (0 == g_dtu_send->flag)
    {
        at_server_printfln("OK");
    }
}

static at_result_t at_wiota_dtu_send_setup(const char *args)
{
    if ((AT_WIOTA_RUN != wiota_state) || (RT_NULL == g_dtu_send))
    {
        return AT_RESULT_FAILE;
    }
    memset(g_dtu_send->exit_flag, 0, WIOTA_TRANS_END_STRING_MAX);
    int timeout = 0;
    int wait = 0;
    args = parse((char *)(++args), "d,d,s", &timeout, &wait, (sl32_t)WIOTA_TRANS_END_STRING_MAX, g_dtu_send->exit_flag);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    g_dtu_send->timeout = timeout & 0xFFFF;
    g_dtu_send->wait = wait & 0xFFFF;
    g_dtu_send->flag = 1;
    return AT_RESULT_OK;
}

static at_result_t at_wiota_dtu_send_exec(void)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    g_dtu_send->flag = 1;
    return AT_RESULT_OK;
}
/*
static at_result_t at_wiotarecv_setup(const char *args)
{
    unsigned short timeout = 0;
    uc_recv_back_t result;

    if (AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    args = parse((char *)(++args), "d", &timeout);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (timeout < 1)
        timeout = WIOTA_WAIT_DATA_TIMEOUT;

    // rt_kprintf("timeout = %d\n", timeout);

    uc_wiota_recv_data(&result, timeout, RT_NULL);
    if (!result.result)
    {
        if (result.type < UC_RECV_MAX_TYPE)
        {
            at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);
        }
        at_send_data(result.data, result.data_len);
        at_server_printfln("");
        rt_free(result.data);
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_FAILE;
    }
}

static at_result_t at_wiota_recv_exec(void)
{
    uc_recv_back_t result;

    if (AT_WIOTA_DEFAULT == wiota_state)
    {
        return AT_RESULT_FAILE;
    }

    uc_wiota_recv_data(&result, WIOTA_WAIT_DATA_TIMEOUT, RT_NULL);
    if (!result.result)
    {
        if (result.type < UC_RECV_MAX_TYPE)
        {
            at_server_printf("+WIOTARECV,%d,%d,", result.type, result.data_len);
        }
        at_send_data(result.data, result.data_len);
        at_server_printfln("");
        rt_free(result.data);
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_FAILE;
    }
}
*/
static at_result_t at_wiotalpm_setup(const char *args)
{
    int mode = 0, value = 0, value2 = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d,d,d", &mode, &value, &value2);

    switch (mode)
    {
    case AT_WIOTA_SLEEP:
    {
        at_server_printfln("OK");
        uart_wait_tx_done();
        uc_wiota_sleep_enter((unsigned char)value, (unsigned char)value2);
        break;
    }
    case AT_WIOTA_PAGING_TX:
    {
        uc_wiota_paging_tx_start();
        break;
    }
    case AT_WIOTA_PAGING_RX:
    {
        // WIOTA_MUST_RUN(wiota_state)
        at_server_printfln("OK");
        uart_wait_tx_done();
        uc_wiota_paging_rx_enter((unsigned char)value, (unsigned char)value2);
        break;
    }
    case AT_WIOTA_CLOCK:
    {
        uc_wiota_set_alarm_time((unsigned int)value);
        break;
    }
    case AT_WIOTA_GATING:
    {
        uc_wiota_set_is_gating((unsigned char)value);
        break;
    }
    case AT_WIOTA_LPM:
    {
        uc_wiota_set_lpm_mode((unsigned char)value);
        break;
    }
    case AT_WIOTA_FREQ_DIV:
    {
        uc_wiota_set_freq_div((unsigned char)value);
        break;
    }
    case AT_WIOTA_VOL_MODE:
    {
        uc_wiota_set_vol_mode((unsigned char)value);
        break;
    }
    case AT_WIOTA_EX_WK:
    {
        uc_wiota_set_is_ex_wk((unsigned int)value);
        break;
    }
    default:
        return AT_RESULT_FAILE;
    }
    return AT_RESULT_OK;
}

static at_result_t at_wiotarate_setup(const char *args)
{
    int rate_mode = 0xFF;
    int rate_value = 0xFF;

    args = parse((char *)(++args), "d,d", &rate_mode, &rate_value);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    at_server_printfln("+WIOTARATE: %d, %d", (unsigned char)rate_mode, (unsigned short)rate_value);
    uc_wiota_set_data_rate((unsigned char)rate_mode, (unsigned short)rate_value);

    return AT_RESULT_OK;
}

static at_result_t at_wiotapow_setup(const char *args)
{
    int mode = 0;
    int power = 0x7F;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    args = parse((char *)(++args), "d,d", &mode, &power);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    // at can't parse minus value for now
    if (mode == 0)
    {
        uc_wiota_set_cur_power((signed char)(power - 20));
    }
    else if (mode == 1)
    {
        uc_wiota_set_max_power((signed char)(power - 20));
    }

    return AT_RESULT_OK;
}

#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)

void at_handle_log_uart(int uart_number)
{
    rt_device_t device = NULL;
    //    rt_device_t old_device = NULL;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT; /*init default parment*/

    device = rt_device_find(AT_SERVER_DEVICE);

    if (device)
    {
        rt_device_close(device);
    }

    if (0 == uart_number)
    {
        config.baud_rate = BAUD_RATE_460800;
        rt_console_set_device(AT_SERVER_DEVICE);
        //boot_set_uart0_baud_rate(BAUD_RATE_460800);
    }
    else if (1 == uart_number)
    {
        config.baud_rate = BAUD_RATE_115200;
        rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
        //boot_set_uart0_baud_rate(BAUD_RATE_115200);
    }

    if (device)
    {
        rt_device_control(device, RT_DEVICE_CTRL_CONFIG, &config);
        rt_device_open(device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    }
}

#endif

static at_result_t at_wiotalog_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    switch (mode)
    {
    case AT_LOG_CLOSE:
    case AT_LOG_OPEN:
        uc_wiota_log_switch(UC_LOG_UART, mode - AT_LOG_CLOSE);
        break;

    case AT_LOG_UART0:
    case AT_LOG_UART1:
#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE)
        at_handle_log_uart(mode - AT_LOG_UART0);
#endif
        break;

    case AT_LOG_SPI_CLOSE:
    case AT_LOG_SPI_OPEN:
        uc_wiota_log_switch(UC_LOG_SPI, mode - AT_LOG_SPI_CLOSE);
        break;

    default:
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotathrought_query(void)
{
    uc_throughput_info_t local_throught_t = {0};

    uc_wiota_get_throughput(&local_throught_t);

    at_server_printfln("+WIOTATHROUGHT=0,%d,%d,%d,%d", local_throught_t.uni_send_succ_data_len, local_throught_t.bc_send_succ_data_len,
                       local_throught_t.uni_recv_succ_data_len, local_throught_t.bc_recv_succ_data_len);

    return AT_RESULT_OK;
}

static at_result_t at_wiotastats_query(void)
{
    uc_stats_info_t local_stats_t = {0};

    uc_wiota_get_all_stats(&local_stats_t);

    at_server_printfln("+WIOTASTATS=0,%d,%d,%d,%d,%d,%d,%d,%d", local_stats_t.uni_send_total, local_stats_t.uni_send_succ,
                       local_stats_t.bc_send_total, local_stats_t.bc_send_succ,
                       local_stats_t.uni_recv_fail, local_stats_t.uni_recv_succ,
                       local_stats_t.bc_recv_fail, local_stats_t.bc_recv_succ);

    return AT_RESULT_OK;
}

static at_result_t at_wiotastats_setup(const char *args)
{
    int mode = 0;
    int type = 0;
    unsigned int back_stats;

    args = parse((char *)(++args), "d,d", &mode, &type);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (UC_STATS_READ == mode)
    {
        if (UC_STATS_TYPE_ALL == type)
        {
            at_wiotastats_query();
        }
        else
        {
            back_stats = uc_wiota_get_stats((unsigned char)type);
            at_server_printfln("+WIOTASTATS=%d,%d", type, back_stats);
        }
    }
    else if (UC_STATS_WRITE == mode)
    {
        uc_wiota_reset_stats((unsigned char)type);
    }
    else
    {
        return AT_RESULT_FAILE;
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotacrc_query(void)
{
    at_server_printfln("+WIOTACRC=%d", uc_wiota_get_crc());

    return AT_RESULT_OK;
}

static at_result_t at_wiotacrc_setup(const char *args)
{
    int crc_limit = 0;

    args = parse((char *)(++args), "d", &crc_limit);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_crc((unsigned short)crc_limit);

    return AT_RESULT_OK;
}

static at_result_t at_wiotaosc_query(void)
{
    at_server_printfln("+WIOTAOSC=%d", uc_wiota_get_is_osc());

    return AT_RESULT_OK;
}

static at_result_t at_wiotaosc_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_is_osc((unsigned char)mode);

    return AT_RESULT_OK;
}

static at_result_t at_wiotalight_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_light_func_enable((unsigned char)mode);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_save_static_exec(void)
{
    WIOTA_CHECK_AUTOMATIC_MANAGER();

    uc_wiota_save_static_info();

    return AT_RESULT_OK;
}

static at_result_t at_wiota_subframe_num_query(void)
{
    unsigned char subframe_num = uc_wiota_get_subframe_num();

    at_server_printfln("+WIOTASUBFNUM:%d", subframe_num);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_subframe_num_setup(const char *args)
{
    int number = 0;

    args = parse((char *)(++args), "d", &number);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (uc_wiota_set_subframe_num((unsigned char)number))
    {
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_PARSE_FAILE;
    }
}

static at_result_t at_wiota_bc_round_setup(const char *args)
{
    int number = 0;

    args = parse((char *)(++args), "d", &number);

    if (!args || number <= 0)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_bc_round((unsigned char)number);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_detect_time_setup(const char *args)
{
    int number = 0;

    args = parse((char *)(++args), "d", &number);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_detect_time((unsigned int)number);

    return AT_RESULT_OK;
}

// static at_result_t at_wiota_bandwidth_setup(const char *args)
// {
//     int number = 0;

//     args = parse((char *)(++args), "d", &number);

//     if (!args || number < 0)
//     {
//         return AT_RESULT_PARSE_FAILE;
//     }

//     uc_wiota_set_bandwidth((unsigned char)number);

//     return AT_RESULT_OK;
// }

static at_result_t at_wiota_continue_send_setup(const char *args)
{
    int flag = 0;

    args = parse((char *)(++args), "d", &flag);

    if (!args || flag < 0)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_continue_send((unsigned char)flag);

    return AT_RESULT_OK;
}

static at_result_t at_wiota_subframe_mode_setup(const char *args)
{
    int mode = 0;
    int flag = 0;

    args = parse((char *)(++args), "d, d", &mode, &flag);

    if (!args || flag < 0)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if (0 == mode)
    {
        uc_wiota_set_subframe_send((unsigned char)flag);
    }
    else if (1 == mode)
    {
        uc_wiota_set_subframe_recv((unsigned char)flag);
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiota_incomplete_recv_setup(const char *args)
{
    int flag = -1;

    args = parse((char *)(++args), "d", &flag);

    if (!args || flag < 0)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_incomplete_recv((unsigned char)flag);

    return AT_RESULT_OK;
}

static at_result_t at_wiotatxmode_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    if (0 <= mode && mode <= 3)
    {
        uc_wiota_set_tx_mode((unsigned char)mode);
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotarecvmode_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    if (0 <= mode && mode <= 1)
    {
        uc_wiota_set_recv_mode((unsigned char)mode);
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotaframeinfo_query(void)
{
    unsigned int frame_len_bc = uc_wiota_get_frame_len(0);
    unsigned int frame_len = uc_wiota_get_frame_len(1);
    unsigned int subframe_len = uc_wiota_get_subframe_len();
    unsigned short first_uni_data = uc_wiota_get_subframe_data_len(0, 0, 1);

    at_server_printfln("+WIOTAFRAMEINFO:%d,%d,%d,%d", frame_len_bc, frame_len, subframe_len, first_uni_data);

    return AT_RESULT_OK;
}

static at_result_t at_wiotastate_query(void)
{
    at_server_printfln("+WIOTASTATE:%d", uc_wiota_get_state());

    return AT_RESULT_OK;
}

static at_result_t at_wiotadjustmode_setup(const char *args)
{
    int mode = 0;

    args = parse((char *)(++args), "d", &mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    if (0 <= mode && mode <= 1)
    {
        uc_wiota_set_adjust_mode((unsigned char)mode);
    }

    return AT_RESULT_OK;
}

static at_result_t at_wiotaunifailcnt_setup(const char *args)
{
    int cnt = 0;

    args = parse((char *)(++args), "d", &cnt);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    if (cnt > 0)
    {
        uc_wiota_set_unisend_fail_cnt((unsigned char)cnt);
    }

    return AT_RESULT_OK;
}

static u32_t nth_power(u32_t num, u32_t n)
{
    u32_t s = 1;

    for (u32_t i = 0; i < n; i++)
    {
        s *= num;
    }
    return s;
}

static void convert_string_to_int(u8_t numLen, u16_t num, const u8_t *pStart, uc_freq_scan_req_p array)
{
    u8_t *temp = NULL;
    u8_t len = 0;
    u8_t nth = numLen;
    u16_t tempNum = 0;

    temp = (u8_t *)rt_malloc(numLen);
    if (temp == NULL)
    {
        rt_kprintf("convert_string_to_int malloc failed\n");
        return;
    }

    for (len = 0; len < numLen; len++)
    {
        temp[len] = pStart[len] - '0';
        tempNum += nth_power(10, nth - 1) * temp[len];
        nth--;
    }
    memcpy((array + num), &tempNum, sizeof(u16_t));
    rt_free(temp);
    temp = NULL;
}

static u16_t convert_string_to_array(u8_t *string, uc_freq_scan_req_p array)
{
    u8_t *pStart = string;
    u8_t *pEnd = string;
    u16_t num = 0;
    u8_t numLen = 0;

    while (*pStart != '\0')
    {
        while (*pEnd != '\0')
        {
            if (*pEnd == ',')
            {
                convert_string_to_int(numLen, num, pStart, array);
                num++;
                pEnd++;
                pStart = pEnd;
                numLen = 0;
            }
            numLen++;
            pEnd++;
        }

        convert_string_to_int(numLen, num, pStart, array);
        num++;
        pStart = pEnd;
    }
    return num;
}

static at_result_t at_scan_freq_setup(const char *args)
{
    u32_t freqNum = 0;
    u32_t timeout = 0;
    u8_t *freqString = RT_NULL;
    u8_t *tempFreq = RT_NULL;
    uc_recv_back_t result;
    u32_t convertNum = 0;
    uc_freq_scan_req_p freqArry = NULL;
    u32_t dataLen = 0;
    u32_t strLen = 0;
    u32_t round = 0;

    WIOTA_CHECK_AUTOMATIC_MANAGER();

    if (wiota_state != AT_WIOTA_RUN)
    {
        return AT_RESULT_REPETITIVE_FAILE;
    }

    args = parse((char *)(++args), "d,d,d,d", &timeout, &round, &dataLen, &freqNum);
    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    strLen = dataLen;

    if (freqNum > 0)
    {
        freqString = (u8_t *)rt_malloc(dataLen);
        if (freqString == RT_NULL)
        {
            at_server_printfln("SEND FAIL");
            return AT_RESULT_NULL;
        }
        tempFreq = freqString;
        at_server_printfln("OK");
        at_server_printf(">");
        while (dataLen)
        {
            if (get_char_timeout(rt_tick_from_millisecond(WIOTA_WAIT_DATA_TIMEOUT), (char *)tempFreq) != RT_EOK)
            {
                at_server_printfln("get char failed!");
                rt_free(freqString);
                freqString = NULL;
                return AT_RESULT_NULL;
            }
            dataLen--;
            tempFreq++;
        }

        freqArry = (uc_freq_scan_req_p)rt_malloc(freqNum * sizeof(uc_freq_scan_req_t));
        if (freqArry == NULL)
        {
            rt_free(freqString);
            freqString = NULL;
            return AT_RESULT_NULL;
        }
        rt_memset(freqArry, 0, freqNum * sizeof(uc_freq_scan_req_t));

        freqString[strLen - 2] = '\0';

        convertNum = convert_string_to_array(freqString, freqArry);
        if (convertNum != freqNum)
        {
            rt_free(freqString);
            freqString = NULL;
            rt_free(freqArry);
            freqArry = NULL;
            return AT_RESULT_FAILE;
        }
        rt_free(freqString);
        freqString = NULL;

        uc_wiota_scan_freq((u8_t *)freqArry, (u16_t)(freqNum * sizeof(uc_freq_scan_req_t)), (u8_t)round, timeout, RT_NULL, &result);

        rt_free(freqArry);
        freqArry = NULL;
    }
    else
    {
        // uc_wiota_scan_freq(RT_NULL, 0, WIOTA_SCAN_FREQ_TIMEOUT, RT_NULL, &result);
        uc_wiota_scan_freq(RT_NULL, 0, (u8_t)round, 0, RT_NULL, &result); // scan all wait for ever
    }

    if (UC_OP_SUCC == result.result)
    {
        uc_freq_scan_result_p freqlinst = (uc_freq_scan_result_p)(result.data);
        int freq_num = result.data_len / sizeof(uc_freq_scan_result_t);

        at_server_printfln("+WIOTASCANFREQ:");

        for (int i = 0; i < freq_num; i++)
        {
            at_server_printfln("%d,%d", freqlinst->freq_idx, freqlinst->rssi);
            freqlinst++;
        }

        rt_free(result.data);
    }
    else
    {
        return AT_RESULT_NULL;
    }

    return AT_RESULT_OK;
}

static at_result_t at_paging_tx_config_query(void)
{
    uc_lpm_tx_cfg_t config;
    uc_wiota_get_paging_tx_cfg(&config);
    at_server_printfln("+WIOTAPAGINGTX=%d,%d,%d,%d,%d,%d",
                       config.freq, config.spectrum_idx, config.bandwidth,
                       config.symbol_length, config.awaken_id, config.send_time);

    return AT_RESULT_OK;
}

static at_result_t at_paging_tx_config_setup(const char *args)
{
    uc_lpm_tx_cfg_t config = {0};
    unsigned int temp[6];
    unsigned char set_ok = FALSE;

    WIOTA_MUST_ALREADY_INIT(wiota_state)

    args = parse((char *)(++args), "d,d,d,d,d,d",
                 &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5]);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    config.freq = (unsigned char)temp[0];
    config.spectrum_idx = (unsigned char)temp[1];
    config.bandwidth = (unsigned char)temp[2];
    config.symbol_length = (unsigned char)temp[3];
    config.awaken_id = (unsigned short)temp[4];
    config.send_time = (unsigned int)temp[5];

    set_ok = uc_wiota_set_paging_tx_cfg(&config);

    if (set_ok)
    {
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_PARSE_FAILE;
    }
}

static at_result_t at_paging_rx_config_query(void)
{
    uc_lpm_rx_cfg_t config;
    uc_wiota_get_paging_rx_cfg(&config);
    at_server_printfln("+WIOTAPAGINGRX=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                       config.freq, config.spectrum_idx, config.bandwidth,
                       config.symbol_length, config.awaken_id, config.detect_period,
                       config.lpm_nlen, config.lpm_utimes, config.threshold,
                       config.extra_flag, config.extra_period);

    return AT_RESULT_OK;
}

static at_result_t at_paging_rx_config_setup(const char *args)
{
    uc_lpm_rx_cfg_t config = {0};
    unsigned int temp[11];
    unsigned char set_ok = FALSE;

    // WIOTA_MUST_ALREADY_INIT(wiota_state)

    args = parse((char *)(++args), "d,d,d,d,d,d,d,d,d,d,d",
                 &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5], &temp[6], &temp[7], &temp[8], &temp[9], &temp[10]);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    config.freq = (unsigned char)temp[0];
    config.spectrum_idx = (unsigned char)temp[1];
    config.bandwidth = (unsigned char)temp[2];
    config.symbol_length = (unsigned char)temp[3];
    config.awaken_id = (unsigned short)temp[4];
    config.detect_period = (unsigned int)temp[5];
    config.lpm_nlen = (unsigned char)temp[6];
    config.lpm_utimes = (unsigned char)temp[7];
    config.threshold = (unsigned short)temp[8];
    config.extra_flag = (unsigned short)temp[9];
    config.extra_period = (unsigned int)temp[10];

    set_ok = uc_wiota_set_paging_rx_cfg(&config);

    if (set_ok)
    {
        return AT_RESULT_OK;
    }
    else
    {
        return AT_RESULT_PARSE_FAILE;
    }
}

static at_result_t at_wiota_symbol_mode_setup(const char *args)
{
    int symbol_mode = 0;

    args = parse((char *)(++args), "d", &symbol_mode);

    if (!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    uc_wiota_set_symbol_mode((unsigned char)(symbol_mode & 0x3));

    return AT_RESULT_OK;
}

static at_result_t at_memory_query(void)
{
    unsigned int total = 0;
    unsigned int used = 0;
    unsigned int max_used = 0;
#ifndef RT_USING_MEMHEAP_AS_HEAP
#if defined(RT_USING_HEAP) && defined(RT_USING_SMALL_MEM)
    rt_memory_info(&total, &used, &max_used);
    rt_kprintf("total %d used %d maxused %d\n", total, used, max_used);
#endif
#endif

    at_server_printfln("+MEMORY=%d,%d,%d", total, used, max_used);

    return AT_RESULT_OK;
}

unsigned int id_list[2] = {0x87654321 ,0x12345678};
unsigned int recv_id = 0;
static at_result_t at_fast_boot_setup(const char *args)
{
    unsigned int len = rt_strlen(args);
    unsigned int mode;
    unsigned int isRecvier;
    unsigned int freq = 45;
    unsigned int mode_num = 3;
    unsigned int flag = 0;
    for(unsigned int i = 0; i < len; i++)
    {
        if(args[i] == ',')
            flag += 1; 
    }
    if(flag == 1)
    {
        args = parse((char *)(++args), "d,d",
                &mode, &isRecvier);
        
    }
    else if(flag == 2)
    {
        args = parse((char *)(++args), "d,d,d",
                        &mode, &isRecvier, &freq);
    }
    else
    {
        return AT_RESULT_PARSE_FAILE;
    }

    if(!args || mode > mode_num || isRecvier > 2)
    {
        return AT_RESULT_PARSE_FAILE;
    }

    unsigned int config_list[3][4]={
        {1, 1, 8, 3},         //普通模式 256，200K
        {0, 1, 8, 3},         //高速模式 128，200K
        {3, 4, 8, 3}          //远传模式 1024，25K
    };

    
    if(uc_wiota_get_state() != UC_STATUS_NULL)
    {
        uc_wiota_exit();
        at_server_printfln("+WIOTARUN=0: OK");
    }
    uc_wiota_init();
    at_server_printfln("+WIOTAINIT: OK");

    unsigned int id = id_list[isRecvier];
    recv_id = id_list[(isRecvier + 1) % 2];
    uc_wiota_set_userid(&id, 4);
    at_server_printfln("+WIOTAUSERID=0x%x: OK",id);

    sub_system_config_t config;
    uc_wiota_get_system_config(&config);
    if(mode != mode_num)
    {
        config.symbol_length = (unsigned char)config_list[mode][0];
        config.bandwidth = (unsigned char)config_list[mode][1];
        config.pz = (unsigned char)config_list[mode][2];
        config.freq_idx = (unsigned char)config_list[mode][3];
        config.subsystemid = 0x21456981;
    }
    uc_wiota_set_system_config(&config);
    at_server_printfln("+WIOTASYSTEMCONFIG=%d,%d,%d,%d,%d,%d,0x%x,0x%x: OK",
                       config.id_len, config.symbol_length, config.bandwidth, config.pz,
                       config.btvalue, config.spectrum_idx, 0, config.subsystemid);

    uc_wiota_set_freq_info(freq);
    at_server_printfln("+WIOTAFREQ=%d: OK", uc_wiota_get_freq_info());

    uc_wiota_set_subframe_num(8);
    unsigned char subframe_num = uc_wiota_get_subframe_num();
    at_server_printfln("+WIOTASUBFNUM:%d: OK", subframe_num);
    
    uc_wiota_set_bc_round(3);
    at_server_printfln("+WIOTABCROUND=3: OK");

    uc_wiota_set_cur_power(0);
    at_server_printfln("+WIOTAPOWER=0: OK");

    uc_wiota_set_data_rate(UC_RATE_NORMAL, UC_MCS_LEVEL_0);
    at_server_printfln("+WIOTAMCS=0: OK");

    uc_wiota_run();
    at_server_printfln("+WIOTARUN=1: OK");
    uc_wiota_register_recv_data_callback(wiota_recv_callback, UC_CALLBACK_NORAMAL_MSG);

    wiota_state = AT_WIOTA_RUN;

    
    return AT_RESULT_OK;
}

static at_result_t at_fast_send(void)
{
    if(wiota_state != AT_WIOTA_RUN)
    {
        return AT_RESULT_FAILE;
    }
    unsigned char data[]="Hello Wiota!";
    unsigned short data_len = rt_strlen((char *)data);
    unsigned int timeout = 20000;
    at_server_printfln("Send to 0x%x", recv_id);
    if(UC_OP_SUCC == uc_wiota_send_data(recv_id, data, data_len, RT_NULL, 0, timeout, RT_NULL))
    {
        at_server_printfln("Send succ");
    }
    else
    {
        at_server_printfln("Send fail");
        return AT_RESULT_NULL;
    }
    return AT_RESULT_OK;
}

#define CYCLE_LEN_MAX       512
static rt_thread_t cycle_thread = RT_NULL;
static unsigned int cycle_delay_time = 5000;
static char cycle_data[CYCLE_LEN_MAX] = "Hello Wiota!";
static unsigned int cycle_len = 12;
static unsigned int cycle_recv_id = 0x12345678;
static unsigned int cyclye_timeout = 5000;

static void cycle_thread_entry(void *parameter)
{
    at_server_printfln("cycle thread");
    unsigned int test_num = 1;
    UC_OP_RESULT send_result;
    while(1)
    {
        at_server_printfln("==========The %dTH cycle send========", test_num);
        send_result = uc_wiota_send_data(cycle_recv_id, (unsigned char *)cycle_data, cycle_len, RT_NULL, 0, cyclye_timeout, RT_NULL);
        if(send_result == UC_OP_SUCC)
        {
            at_server_printfln("Send succ");
        }
        else if(send_result == UC_OP_TIMEOUT)
        {
            at_server_printfln("Send timeout");
        }
        else
        {
            at_server_printfln("Send fail");
        }
        test_num += 1;
        rt_thread_delay(cycle_delay_time);
    }
}

static at_result_t cycle_thread_create(void)
{
    if(cycle_thread != RT_NULL)
    {
        rt_thread_delete(cycle_thread);
        cycle_thread = RT_NULL;
    }
    cycle_thread = rt_thread_create("cycle_send",
                                    cycle_thread_entry,
                                    RT_NULL,
                                    5120,
                                    5,
                                    10);
    if(cycle_thread != RT_NULL)
    {
        rt_thread_startup(cycle_thread);
        return AT_RESULT_OK;
    }

    return AT_RESULT_FAILE;
}

static at_result_t at_cycle_send_setup(const char *args)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    args = parse((char *)(++args),"d,d,d,y",
                    &cyclye_timeout, &cycle_len, &cycle_delay_time, &cycle_recv_id);
    if(!args)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    if(cycle_len >= CYCLE_LEN_MAX || cycle_len == 0)
    {
        return AT_RESULT_PARSE_FAILE;
    }
    char *data  = rt_malloc(cycle_len);
    for(unsigned int i = 0; i < cycle_len; i++)
    {
        data[i] = 'a' + (i % 26);
    }
    rt_strncpy(cycle_data, data, cycle_len);
    cycle_data[cycle_len] = '\0';
    rt_free(data);
    at_server_printfln("timeout = %d\nlen = %d\ndelay_time = %d\nrecv_id = 0x%x",cyclye_timeout,cycle_len,cycle_delay_time,cycle_recv_id);
    at_send_data(cycle_data, cycle_len);

    return cycle_thread_create();
}

static at_result_t at_cycle_send_exec(void)
{
    if (AT_WIOTA_RUN != wiota_state)
    {
        return AT_RESULT_FAILE;
    }
    cycle_len = 12;
    char data[] = "Hello Wiota!";
    unsigned short data_len = rt_strlen(data);
    rt_strncpy(cycle_data, data, data_len);
    cycle_delay_time = 5000;
    cycle_recv_id = 0x12345678;
    cyclye_timeout = 5000;

    return cycle_thread_create();
}

static at_result_t at_cycle_send_end(void)
{
    if(cycle_thread != RT_NULL)
    {
        rt_thread_delete(cycle_thread);
        cycle_thread = RT_NULL;
    }
    return AT_RESULT_OK;
}

AT_CMD_EXPORT("AT+WIOTAVERSION", RT_NULL, RT_NULL, at_wiota_version_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAINIT", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_init_exec);
AT_CMD_EXPORT("AT+WIOTALPM", "=<mode>,<value>,<value2>", RT_NULL, RT_NULL, at_wiotalpm_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARATE", "=<rate_mode>,<rate_value>", RT_NULL, RT_NULL, at_wiotarate_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAPOW", "=<mode>,<power>", RT_NULL, RT_NULL, at_wiotapow_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAFREQ", "=<freqpint>", RT_NULL, at_freq_query, at_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADCXO", "=<dcxo>", RT_NULL, RT_NULL, at_dcxo_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAUSERID", "=<id0>", RT_NULL, at_userid_query, at_userid_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARADIO", RT_NULL, RT_NULL, at_radio_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONFIG", "=<id_len>,<symbol>,<band>,<pz>,<bt>,<spec_idx>,<systemid>,<subsystemid>",
              RT_NULL, at_system_config_query, at_system_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASUBSYSID", "=<subsystemid>", RT_NULL, at_subsys_id_query, at_subsys_id_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARUN", "=<state>", RT_NULL, RT_NULL, at_wiota_cfun_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASEND", "=<timeout>,<len>,<userId>", RT_NULL, RT_NULL, at_wiotasend_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTATRANS", "=<timeout>,<end>", RT_NULL, RT_NULL, at_wiotatrans_setup, at_wiotatrans_exec);
AT_CMD_EXPORT("AT+WIOTADTUSEND", "=<timeout>,<wait>,<end>", RT_NULL, RT_NULL, at_wiota_dtu_send_setup, at_wiota_dtu_send_exec);
// AT_CMD_EXPORT("AT+WIOTARECV", "=<timeout>", RT_NULL, RT_NULL, at_wiotarecv_setup, at_wiota_recv_exec);
AT_CMD_EXPORT("AT+WIOTALOG", "=<mode>", RT_NULL, RT_NULL, at_wiotalog_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASTATS", "=<mode>,<type>", RT_NULL, at_wiotastats_query, at_wiotastats_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTATHROUGHT", "=<mode>,<type>", RT_NULL, at_wiotathrought_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACRC", "=<crc_limit>", RT_NULL, at_wiotacrc_query, at_wiotacrc_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAOSC", "=<mode>", RT_NULL, at_wiotaosc_query, at_wiotaosc_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTALIGHT", "=<mode>", RT_NULL, RT_NULL, at_wiotalight_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASAVESTATIC", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_wiota_save_static_exec);
AT_CMD_EXPORT("AT+WIOTASUBNUM", "=<number>", RT_NULL, at_wiota_subframe_num_query, at_wiota_subframe_num_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTABCROUND", "=<round>", RT_NULL, RT_NULL, at_wiota_bc_round_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADETECTIME", "=<time>", RT_NULL, RT_NULL, at_wiota_detect_time_setup, RT_NULL);
// AT_CMD_EXPORT("AT+WIOTABAND", "=<band>", RT_NULL, RT_NULL, at_wiota_bandwidth_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACONTINUESEND", "=<flag>", RT_NULL, RT_NULL, at_wiota_continue_send_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASUBFRAMEMODE", "=<mode>, <flag>", RT_NULL, RT_NULL, at_wiota_subframe_mode_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAINCRECV", "=<flag>", RT_NULL, RT_NULL, at_wiota_incomplete_recv_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTATXMODE", "=<mode>", RT_NULL, RT_NULL, at_wiotatxmode_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTARECVMODE", "=<mode>", RT_NULL, RT_NULL, at_wiotarecvmode_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAFRAMEINFO", RT_NULL, RT_NULL, at_wiotaframeinfo_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASTATE", RT_NULL, RT_NULL, at_wiotastate_query, RT_NULL, RT_NULL);
AT_CMD_EXPORT("AT+WIOTADJUST", "=<mode>", RT_NULL, RT_NULL, at_wiotadjustmode_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAUNIFAIL", "=<cnt>", RT_NULL, RT_NULL, at_wiotaunifailcnt_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASCANFREQ", "=<timeout>,<round>,<dataLen>,<freqnum>", RT_NULL, RT_NULL, at_scan_freq_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAPAGINGTX", "=<freq>,<spec_idx>,<band>,<symbol>,<awaken_id>,<send_time>",
              RT_NULL, at_paging_tx_config_query, at_paging_tx_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAPAGINGRX", "=<freq>,<spec_idx>,<band>,<symbol>,<awaken_id>,<detect_period>,<nlen>,<utimes>,<thres>,<extra_flag>,<extra_period>",
              RT_NULL, at_paging_rx_config_query, at_paging_rx_config_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTASYMBMODE", "=<symbol_mode>", RT_NULL, RT_NULL, at_wiota_symbol_mode_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTACKMEM", "=<total>,<used>,<maxused>", RT_NULL, at_memory_query, RT_NULL, RT_NULL);

AT_CMD_EXPORT("AT+WIOTAFASTBOOT", "=<mode>,<isReceiver>,[<freq>]", RT_NULL, RT_NULL, at_fast_boot_setup, RT_NULL);
AT_CMD_EXPORT("AT+WIOTAFASTSEND", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_fast_send);
AT_CMD_EXPORT("AT+WIOTACYCLESEND", "=<timeout>,<data_len>,<delaytime>,<recvid>", RT_NULL, RT_NULL, at_cycle_send_setup, at_cycle_send_exec);
AT_CMD_EXPORT("AT+WIOTAENDCYCLE", RT_NULL, RT_NULL, RT_NULL, RT_NULL, at_cycle_send_end);

#endif
#endif
#endif
