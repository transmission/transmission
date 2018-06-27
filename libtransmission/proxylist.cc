#include "proxylist.h"
#include "utils.h"
#include "tr-assert.h"

tr_proxy_list& tr_proxy_list::operator=(tr_variant const* variant_string_list)
{
    proxy_strings_.clear();

    if (variant_string_list == nullptr)
    {
        return *this;
    }

    TR_ASSERT(tr_variantIsList(variant_string_list));

    auto count = tr_variantListSize(variant_string_list);

    for (size_t i = 0; i != count; ++i)
    {
        auto item = tr_variantListChild(variant_string_list, i);
        TR_ASSERT(tr_variantIsString(item));

        if (std::string_view str; tr_variantGetStrView(item, &str))
        {
            proxy_strings_.emplace_back(str);
        }

        else
        {
            break;
        }
    }

    return *this;
}

void tr_proxy_list::toVariant(tr_variant* variant_string_list) const
{
    TR_ASSERT(tr_variantIsList(variant_string_list));
    tr_variantFree(variant_string_list);
    tr_variantInitList(variant_string_list, proxy_strings_.size());

    for (auto const& str : proxy_strings_)
    {
        tr_variantListAddStr(variant_string_list, str);
    }
}

std::optional<std::string_view> tr_proxy_list::getProxyUrl(std::string_view tracker_url) const
{
    /* This will be slow for large proxy list, so don't use large lists */
    for (size_t i = 1; i < proxy_strings_.size(); i += 2)
    {
        if (tr_wildmat(tracker_url, proxy_strings_[i - 1]))
        {
            return proxy_strings_[i];
        }
    }

    return std::nullopt;
}
