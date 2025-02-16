#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void randDelay(const float base, const float jit);
void saveProgress(const json& messages);

// declare, definedin main already
extern std::string TOKEN;
extern std::string TARGET_ID;
extern std::string QUERY;
extern std::string SAVE_FILE;
extern bool is_dm_mode;
extern bool verbose_fetch;

#endif //MAIN_H
