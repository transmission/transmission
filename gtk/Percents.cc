#include "Percents.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

std::string Percents::to_string() const
{
    return tr_strpercent(raw_value_ / 100.);
}
