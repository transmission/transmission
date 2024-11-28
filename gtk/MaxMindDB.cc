// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "MaxMindDB.h"

#include "Session.h"
#include <curl/curl.h>
#include <maxminddb.h>

#include <glibmm/miscutils.h>

#include <cstddef>
#include <string>

// Callback function for `curl_easy_setopt` to write data to a file
size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

// Function to download and update the mmdb file
// Free database with monthly updates from <https://db-ip.com/>
void maintain_mmdb_file(std::string mmdb_file)
{
    // UTC year and month
    using namespace std::chrono;
    auto date_time = Glib::DateTime::create_now_utc(system_clock::to_time_t(system_clock::now()));
    std::string year = std::to_string(date_time.get_year());
    std::string month = std::to_string(date_time.get_month()); // TODO: pad

    std::string url = "";
    if (access(mmdb_file.c_str(), F_OK) == -1)
    {
        url = "https://download.db-ip.com/free/dbip-city-lite-" + year + "-" + month + ".mmdb.gz";
    }
    else
    {
        MMDB_s mmdb;
        int status = MMDB_open(mmdb_file.c_str(), MMDB_MODE_MMAP, &mmdb);
        if (MMDB_SUCCESS != status)
        {
            fprintf(stderr, "MMDB: Error while opening database file (%s, %s)\n", mmdb_file.c_str(), MMDB_strerror(status));
            url = "https://download.db-ip.com/free/dbip-city-lite-" + year + "-" + month + ".mmdb.gz";
        }
        else
        {
            // Existing database file's year and month
            time_t build_epoch = mmdb.metadata.build_epoch;
            date_time = Glib::DateTime::create_now_utc(build_epoch);
            std::string mmdb_year = std::to_string(date_time.get_year());
            std::string mmdb_month = std::to_string(date_time.get_month()); // TODO: pad
            if (month != mmdb_month || year != mmdb_year)
            {
                // TODO: delete existing
                url = "https://download.db-ip.com/free/dbip-city-lite-" + year + "-" + month + ".mmdb.gz";
            }
        }
        MMDB_close(&mmdb);
    }

    if (url != "")
    {
        // Download mmdb.gz file
        CURL* curl;
        curl = curl_easy_init();
        if (curl)
        {
            FILE* fp;
            fp = fopen((mmdb_file + ".gz").c_str(), "wb");
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_perform(curl);
            curl_easy_cleanup(curl); // always cleanup
            fclose(fp);

            // Decompress mmdb.gz file
            // TODO: use libdeflate
            system(("gzip -d " + mmdb_file + ".gz").c_str());
        }
        // TODO: handle non-existing file and errors
    }
}

std::string get_location_from_ip(std::string ip)
{
    // Database file path
    std::vector<std::string> path;
    path.push_back(Glib::get_home_dir());
    path.push_back("dbip-city-lite.mmdb");
    std::string mmdb_file = Glib::build_filename(path);

    maintain_mmdb_file(mmdb_file);

    // Compute location from IP
    MMDB_s mmdb;
    int status = MMDB_open(mmdb_file.c_str(), MMDB_MODE_MMAP, &mmdb);
    if (MMDB_SUCCESS != status)
    {
        fprintf(stderr, "MMDB: Error while opening database file (%s, %s)\n", mmdb_file.c_str(), MMDB_strerror(status));
        return "";
    }
    int gai_error, mmdb_error;
    MMDB_lookup_result_s result = MMDB_lookup_string(&mmdb, ip.c_str(), &gai_error, &mmdb_error);
    if (0 != gai_error || MMDB_SUCCESS != mmdb_error || !result.found_entry)
    {
        fprintf(stderr, "MMDB: Error while looking up IP %s\n", ip.c_str());
        MMDB_close(&mmdb);
        return "";
    }
    MMDB_entry_data_s entry_data;
    status = MMDB_get_value(&result.entry, &entry_data, "city", "names", "en", NULL);
    if (MMDB_SUCCESS != status || !entry_data.has_data)
    {
        fprintf(stderr, "MMDB: Error while getting value of city for IP %s\n", ip.c_str());
        MMDB_close(&mmdb);
        return "";
    }
    std::string location = "";
    location.append(entry_data.utf8_string, entry_data.utf8_string + entry_data.data_size);
    location += ", ";
    status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
    if (MMDB_SUCCESS != status || !entry_data.has_data)
    {
        fprintf(stderr, "MMDB: Error while getting value of country for IP %s\n", ip.c_str());
        MMDB_close(&mmdb);
        return "";
    }
    location.append(entry_data.utf8_string, entry_data.utf8_string + entry_data.data_size);
    MMDB_close(&mmdb);
    return location;
}
