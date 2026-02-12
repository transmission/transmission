#include "Percents.h"

#include <lib/transmission/utils.h>

std::string Percents::to_string() const
{
    return tr_strpercent(raw_value_ / 100.);
}
