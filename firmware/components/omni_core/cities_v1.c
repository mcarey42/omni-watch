/* The city table as Stage 1 wrote it (77e1124..0cfbb17, unchanged throughout), name and country only, in index
 * order: a version 1 settings blob holds an index into this table, not the current one (m4, R62). Frozen: never
 * regenerate or edit it; tools/gen_cities.py writes cities.c only. */
#include "omni/cities.h"

static const struct { const char *name, *country; } s_v1[] = {
    { "Abidjan", "CI" }, { "Accra", "GH" }, { "Addis Ababa", "ET" }, { "Adelaide", "AU" }, { "Algiers", "DZ" },
    { "Almaty", "KZ" }, { "Amman", "JO" }, { "Amsterdam", "NL" }, { "Anchorage", "US" }, { "Antananarivo", "MG" },
    { "Apia", "WS" }, { "Asuncion", "PY" }, { "Athens", "GR" }, { "Atlanta", "US" }, { "Auckland", "NZ" },
    { "Azores", "PT" }, { "Baghdad", "IQ" }, { "Baku", "AZ" }, { "Bangkok", "TH" }, { "Beijing", "CN" },
    { "Beirut", "LB" }, { "Belgrade", "RS" }, { "Berlin", "DE" }, { "Bermuda", "BM" }, { "Bogota", "CO" },
    { "Boise", "US" }, { "Boston", "US" }, { "Brasilia", "BR" }, { "Bratislava", "SK" }, { "Brisbane", "AU" },
    { "Brussels", "BE" }, { "Bucharest", "RO" }, { "Budapest", "HU" }, { "Buenos Aires", "AR" }, { "Cairo", "EG" },
    { "Cancun", "MX" }, { "Caracas", "VE" }, { "Casablanca", "MA" }, { "Chicago", "US" }, { "Chisinau", "MD" },
    { "Colombo", "LK" }, { "Columbus", "US" }, { "Copenhagen", "DK" }, { "Dakar", "SN" }, { "Dallas", "US" },
    { "Damascus", "SY" }, { "Dar es Salaam", "TZ" }, { "Darwin", "AU" }, { "Delhi", "IN" }, { "Denver", "US" },
    { "Detroit", "US" }, { "Dhaka", "BD" }, { "Doha", "QA" }, { "Dubai", "AE" }, { "Dublin", "IE" },
    { "Edmonton", "CA" }, { "Fiji", "FJ" }, { "Georgetown", "GY" }, { "Guam", "GU" }, { "Guatemala", "GT" },
    { "Halifax", "CA" }, { "Harare", "ZW" }, { "Havana", "CU" }, { "Helsinki", "FI" }, { "Ho Chi Minh City", "VN" },
    { "Hobart", "AU" }, { "Hong Kong", "HK" }, { "Honolulu", "US" }, { "Houston", "US" }, { "Indianapolis", "US" },
    { "Istanbul", "TR" }, { "Jakarta", "ID" }, { "Jerusalem", "IL" }, { "Johannesburg", "ZA" }, { "Juneau", "US" },
    { "Kabul", "AF" }, { "Kampala", "UG" }, { "Karachi", "PK" }, { "Kathmandu", "NP" }, { "Khartoum", "SD" },
    { "Kingston", "JM" }, { "Kinshasa", "CD" }, { "Kolkata", "IN" }, { "Kuala Lumpur", "MY" }, { "Kuwait", "KW" },
    { "Kyiv", "UA" }, { "La Paz", "BO" }, { "Lagos", "NG" }, { "Las Vegas", "US" }, { "Lima", "PE" },
    { "Lisbon", "PT" }, { "Ljubljana", "SI" }, { "London", "GB" }, { "Los Angeles", "US" }, { "Louisville", "US" },
    { "Luanda", "AO" }, { "Luxembourg", "LU" }, { "Madrid", "ES" }, { "Managua", "NI" }, { "Manaus", "BR" },
    { "Manila", "PH" }, { "Maputo", "MZ" }, { "Mauritius", "MU" }, { "Melbourne", "AU" }, { "Mexico City", "MX" },
    { "Miami", "US" }, { "Minsk", "BY" }, { "Monaco", "MC" }, { "Monterrey", "MX" }, { "Montevideo", "UY" },
    { "Montreal", "CA" }, { "Moscow", "RU" }, { "Mumbai", "IN" }, { "Nairobi", "KE" }, { "Nassau", "BS" },
    { "New York", "US" }, { "Noumea", "NC" }, { "Novosibirsk", "RU" }, { "Nuuk", "GL" }, { "Oslo", "NO" },
    { "Panama", "PA" }, { "Paramaribo", "SR" }, { "Paris", "FR" }, { "Perth", "AU" }, { "Phnom Penh", "KH" },
    { "Phoenix", "US" }, { "Port Moresby", "PG" }, { "Port-au-Prince", "HT" }, { "Prague", "CZ" },
    { "Pyongyang", "KP" }, { "Quito", "EC" }, { "Regina", "CA" }, { "Reykjavik", "IS" }, { "Riga", "LV" },
    { "Rio de Janeiro", "BR" }, { "Riyadh", "SA" }, { "Rome", "IT" }, { "San Francisco", "US" }, { "San Jose", "CR" },
    { "San Juan", "PR" }, { "San Salvador", "SV" }, { "Santiago", "CL" }, { "Santo Domingo", "DO" },
    { "Sao Paulo", "BR" }, { "Sarajevo", "BA" }, { "Seattle", "US" }, { "Seoul", "KR" }, { "Shanghai", "CN" },
    { "Singapore", "SG" }, { "Skopje", "MK" }, { "Sofia", "BG" }, { "St. John's", "CA" }, { "Stockholm", "SE" },
    { "Sydney", "AU" }, { "Tahiti", "PF" }, { "Taipei", "TW" }, { "Tallinn", "EE" }, { "Tashkent", "UZ" },
    { "Tbilisi", "GE" }, { "Tegucigalpa", "HN" }, { "Tehran", "IR" }, { "Tijuana", "MX" }, { "Tirana", "AL" },
    { "Tokyo", "JP" }, { "Tongatapu", "TO" }, { "Toronto", "CA" }, { "Tripoli", "LY" }, { "Tunis", "TN" },
    { "Ulaanbaatar", "MN" }, { "Valletta", "MT" }, { "Vancouver", "CA" }, { "Vienna", "AT" }, { "Vientiane", "LA" },
    { "Vilnius", "LT" }, { "Vladivostok", "RU" }, { "Warsaw", "PL" }, { "Washington", "US" }, { "Whitehorse", "CA" },
    { "Windhoek", "NA" }, { "Winnipeg", "CA" }, { "Yangon", "MM" }, { "Yekaterinburg", "RU" }, { "Yerevan", "AM" },
    { "Zagreb", "HR" }, { "Zurich", "CH" },
};

int omni_city_from_v1(uint16_t v1_index)
{
    if (v1_index >= sizeof s_v1 / sizeof s_v1[0]) return -1;
    return omni_city_find_key(s_v1[v1_index].name, s_v1[v1_index].country);
}
