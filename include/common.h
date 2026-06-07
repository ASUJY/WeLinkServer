#ifndef COMMON_H
#define COMMON_H

enum E_MSG_TYPE : int {
    UNKNOWN = 0,
    LOGIN_MSG,
    LOGIN_MSG_ACK_SUCCESS,
    LOGIN_MSG_ACK_FAIL,
    REG_MSG,
    REG_MSG_ACK_SUCCESS,
    REG_MSG_ACK_FAIL,
};

#endif