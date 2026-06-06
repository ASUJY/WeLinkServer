#ifndef USER_H
#define USER_H

#include <string>


// USER表的ORM类
class User {
public:
    User(int id = -1, std::string name = "", std::string pwd = "", std::string phone = "", std::string state = "offline") {
        this->m_userid = id;
        this->m_username = name;
        this->m_password = pwd;
        this->m_userphone = phone;
        this->m_userstate = state;
    }

    void SetUserId(int id) {this->m_userid = id;}
    void SetUserName(std::string name) {this->m_username = name;}
    void SetPwd(std::string pwd) {this->m_password = pwd;}
    void SetUserPhone(std::string phone) {this->m_userphone = phone;}
    void SetUserState(std::string state) {this->m_userstate = state;}

    long long GetUserId() {return m_userid;}
    std::string GetUserName() {return m_username;}
    std::string GetPwd() {return m_password;}
    std::string GetUserPhone() {return m_userphone;}
    std::string GetUserState() {return m_userstate;}

private:
    long long m_userid;
    std::string m_userphone;
    std::string m_username;
    std::string m_password;
    std::string m_userstate;    
};

#endif