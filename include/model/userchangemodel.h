#ifndef USERCHANGEMODEl_H
#define USERCHANGEMODEl_H

#include <vector>

#include "user.hpp"

class UserChangeModel {
public:
    bool Insert(User& user);
    std::string QueryState(uint64_t userid);
    bool UpdateState(User& user);
    bool UpdateState(uint64_t userid, const std::string& state);
};

#endif