#!/bin/bash
DING_WEBHOOK=""
BIN_PATH=""
# 告警冷却，防止短时间反复重启疯狂刷屏
COOLDOWN_SEC=60
LAST_ALERT=0

send_ding()
{
    local now=$(date +%s)
    if [ $((now - LAST_ALERT)) -lt ${COOLDOWN_SEC} ];then
        return
    fi
    LAST_ALERT=$now

    TIME=$(date "+%Y-%m-%d %H:%M:%S")
    HOSTNAME=$(hostname)
    IP=$(hostname -I | awk '{print $1}')
    CONTENT="{
        \"msgtype\": \"text\",
        \"text\": {
            \"content\": \"【服务异常告警】\n服务：WeLinkServer\n状态：进程意外终止\n主机：$HOSTNAME ($IP)\n时间：$TIME\nSupervisor 正在自动重启！\"
        }
    }"
    curl -s -X POST "$DING_WEBHOOK" -H 'Content-Type: application/json' -d "$CONTENT"
}

while true
do
    # 检测进程是否存在
    if ! pgrep -f "${BIN_PATH}" >/dev/null;then
        send_ding
    fi
    sleep 2
done