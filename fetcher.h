#ifndef FETCHER_H
#define FETCHER_H


#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

std::pair<long, std::string> dcReq(const std::string& url, const std::string& method = "GET");
nlohmann::json fetchMsgsGuild(const std::string& query);
nlohmann::json fetchMsgsDM(const std::string& query);


#endif //FETCHER_H
