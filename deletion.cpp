#include "deletion.h"
#include <iostream>
#include <string>
#include <random>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "main.h"
#include "fetcher.h"

using json = nlohmann::json;

void delMsgs(json& messages, const bool reverse) {
    const size_t total = messages.size();
    int processed = 0;
    bool needsSaving = false;
    float delayBetweenDel = 1.0f;

    // Create processing order
    std::vector<size_t> indices;
    for (size_t i = 0; i < total; ++i) indices.push_back(i);
    if (reverse) std::ranges::reverse(indices);

    for (const size_t idx : indices) {
        TryDelAgain:
        auto& msg = messages[idx];
        if (msg["deleted"].get<bool>()) {
            processed++;
            continue;
        }

        
        // same shit as before "unnecessary temporary string allocations" real
        std::string url = "https://discord.com/api/v9/channels/";
        url += msg["channel_id"].get<std::string>();
        url += "/messages/";
        url += msg["id"].get<std::string>();
        
        auto [status, response] = dcReq(url, "DELETE");
        
        if (status == 204) {
            msg["deleted"] = true;
            needsSaving = true;
            
            if (processed % 10 == 0) {
                std::cout << "[" << processed + 1 << "/" << total << "] Deleted message " 
                         << msg["id"].get<std::string>() << std::endl;
                //if (needsSaving) {  }
                saveProgress(messages);
                needsSaving = false;
                
            }
        } else {
            msg["last_attempt"] = time(nullptr);
            std::cerr << "Failed to delete " << msg["id"].get<std::string>() 
                     << " (HTTP " << status << ") Response:" << response << std::endl;
        }

        if (status == 429)
        {
            std::cout << "\nOh no! It seems like you are rate limited, lets wait a few seconds to cool down.\t (Response was: " << response << ")" << std::endl;
            randDelay(8.0f, 1.0f);
            if (delayBetweenDel < 2.0f)
            {
                delayBetweenDel += 0.05f;
            }
            std::cout << "Added 5 ms to deletion delay. \nTrying to delete " << processed + 1 << " again..." << std::endl;
            goto TryDelAgain;
        }

        processed++;
        randDelay(delayBetweenDel, 0.2f);
    }

    if (needsSaving) {
        saveProgress(messages);
    }
}