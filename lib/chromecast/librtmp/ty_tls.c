#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "uni_log.h"
#include "uni_thread.h"
#include "adapter_platform.h"

#include "tuya_tls.h"
#include "ty_tls.h"
#include "cloud_operation.h"



static int tuya_tls_rtmp_send_cb( void *ctx, const unsigned char *buf, size_t len )
{
    TLS_CTX s = (TLS_CTX)ctx;

    return unw_send(s->sockfd, buf, len);
}

static int tuya_tls_rtmp_recv_cb( void *ctx, unsigned char *buf, size_t len )
{
    TLS_CTX s = (TLS_CTX)ctx;
    int err_cnt = 0;

    while(err_cnt < 20) {
        int result = unw_recv(s->sockfd, buf, len);
        if(result < 0) {
            UNW_ERRNO_T err = unw_get_errno();
            if(err == UNW_ENOMEM) {
                err_cnt++;
                PR_TRACE("tls http recv no memory.len:%d ret:%d err:%d. try recv again %d", len, result, err, err_cnt);
                SystemSleep(100);
                continue;
            }else if(err == UNW_EAGAIN) {
                err_cnt++;
                PR_TRACE("tls http recv again.len:%d ret:%d err:%d. try recv again %d", len, result, err, err_cnt);
                SystemSleep(10);
                continue;
            } else
            {
                unw_set_block(s->sockfd, TRUE);
                PR_ERR("tls http recv fail.len:%d ret:%d err:%d", len, result, err);
                return result;
            }
        } else {
            unw_set_block(s->sockfd, TRUE);
            err_cnt = 0;
            return result;
        }
    }
    unw_set_block(s->sockfd, TRUE);
    UNW_ERRNO_T err = unw_get_errno();
    PR_ERR("tls http recv fail.len:%d err:%d", len, err);

    return -1;
}

typedef struct ty_stream_url_info_ {
    char tls;
    int port;
    char domain[64];
    char username[64];
    char password[64];
    char path[128];
} ty_stream_url_info_s;

// rtmps://rtc1.tuyaus.com:45633/hls/9828766684487745566
static int tuya_parse_stream_url_info(char const* url, ty_stream_url_info_s* pruinfo)
{
    uint32_t prefixLength = 7;  

    if(strncmp("rtmps", url, strlen("rtmps")) == 0) {
        prefixLength = 8;
        pruinfo->tls = 1;
    }

    char const* from = &url[prefixLength];
    char const* tmpPos = NULL;

    if ((tmpPos = strchr(from, '@')) != NULL) {  
        // found <username> (and perhaps <password>).  
        char const* usernameStart = from;  
        char const* passwordStart = NULL;  
        char const* p = tmpPos;  
  
        if ((tmpPos = strchr(from, ':')) != NULL && tmpPos < p) {  
            passwordStart = tmpPos + 1;
            uint32_t passwordLen = p - passwordStart;  
            strncpy(pruinfo->password, passwordStart, passwordLen);
            pruinfo->password[passwordLen] = '\0'; // Set the ending character.  
        }  
              
        uint32_t usernameLen = 0;  
        if (passwordStart != NULL) {
            usernameLen = tmpPos - usernameStart;  
        } else {  
            usernameLen = p - usernameStart;      
        }         
        strncpy(pruinfo->username, usernameStart, usernameLen);
        pruinfo->username[usernameLen] = '\0';  // Set the ending character.  
  
        from = p + 1; // skip the '@'  
    }  

    const char* pathStart = NULL;  
    if ((tmpPos = strchr(from, '/')) != NULL) {  
        uint32_t pathLen = strlen(tmpPos + 1);  //Skip '/'  
        strncpy(pruinfo->path, tmpPos + 1, pathLen + 1);  
        pathStart = tmpPos;  
    }  
  
    // Next, will parse the address and port.  
    tmpPos = strchr(from, ':');  
    if (tmpPos == NULL) {  
        if (pathStart == NULL) {  
            uint32_t addressLen = strlen(from);  
            strncpy(pruinfo->domain, from, addressLen + 1);  //Already include '\0'  
        } else {  
            uint32_t addressLen = pathStart - from;  
            strncpy(pruinfo->domain, from, addressLen);  
            pruinfo->domain[addressLen] = '\0';   //Set the ending character.  
        }

        pruinfo->port = 554; // Has not the specified port, and will use the default value  
    } else if (tmpPos != NULL) {  
        uint32_t addressLen = tmpPos - from;
        strncpy(pruinfo->domain, from, addressLen);  
        pruinfo->domain[addressLen] = '\0';  //Set the ending character.  
        pruinfo->port = strtoul(tmpPos + 1, NULL, 10);   
    }

    if(strlen(pruinfo->domain) <= 0) {
        return -1;
    }

    return 0;
}

int ty_tls_client_init(TLS_CTX ctx, char const* url)
{
    ty_stream_url_info_s ruinfo;
    int ret = 0;
    CHAR_T ca_url[URL_MAX_LEN] = {0};

    memset(&ruinfo, 0, sizeof(ty_stream_url_info_s));

    tuya_parse_stream_url_info(url, &ruinfo);

    PR_DEBUG("%s, %d\n", ruinfo.domain, ruinfo.port);
    snprintf(ca_url,URL_MAX_LEN,"%s",ruinfo.domain);

    ret = cloud_require_new_ca(ca_url,CA_TYPE_CHROMECAST);
    if(OPRT_OK != ret)
    {
        PR_ERR("update chromecast ca fail%d",ret);
        return ret;
    }


    tuya_tls_connect(&ctx->tls_handler, ruinfo.domain, ruinfo.port, 0,
                    ctx, tuya_tls_rtmp_send_cb, tuya_tls_rtmp_recv_cb,
                    ctx->sockfd, 10);

    return 0;
}

int ty_tls_client_uninit(TLS_CTX ctx)
{
    tuya_tls_disconnect(ctx->tls_handler);

    return 0;
}

