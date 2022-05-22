#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <vector>
#include <string>
#include <optional>
#include <string_view>
#include "variant.h"

class tr_proxy_list
{
public:
    tr_proxy_list& operator=(tr_variant const* variant_string_list);
    void toVariant(tr_variant* variant_string_list) const;
    std::optional<std::string_view> getProxyUrl(std::string_view tracker_url) const;

private:
    std::vector<std::string> proxy_strings_;
};
