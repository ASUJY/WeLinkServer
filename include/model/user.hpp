#ifndef USER_H
#define USER_H

#include <string>


// USER表的ORM类
class User {
public:
    User(int id = -1, std::string phone = "", std::string account = "",
    std::string name = "", std::string avatar = "", std::string pwd = "",  std::string state = "offline") {
        this->m_userid = id;
        this->m_userphone = phone;
        this->m_account = account;
        this->m_username = name;
        this->m_avatar = avatar;
        this->m_password = pwd;
        this->m_userstate = state;
    }

    void SetUserId(int id) {this->m_userid = id;}
    void SetUserPhone(std::string phone) {this->m_userphone = phone;}
    void SetUserAccount(std::string account) {this->m_account = account;}
    void SetUserName(std::string name) {this->m_username = name;}
    void SetUserAvatar(std::string avatar) {this->m_avatar = avatar;}
    void SetPwd(std::string pwd) {this->m_password = pwd;}
    void SetUserState(std::string state) {this->m_userstate = state;}

    long long GetUserId() {return m_userid;}
    std::string GetUserPhone() {return m_userphone;}
    std::string GetUserAccount() {return m_account;}
    std::string GetUserName() {return m_username;}
    std::string GetUserAvatar() {return m_avatar;}
    std::string GetPwd() {return m_password;}
    std::string GetUserState() {return m_userstate;}

private:
    long long m_userid;
    std::string m_userphone;
    std::string m_account;
    std::string m_username;
    std::string m_avatar;
    std::string m_password;
    std::string m_userstate;
};

#endif