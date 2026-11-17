#!/usr/bin/env python3
"""Generate firmware/components/omni_core/cities.c from the system zoneinfo.

Reads <zoneinfo>/zone1970.tab (falling back to zone.tab for zones merged out
of zone1970) for coordinates and country, and the trailing POSIX TZ line of
each TZif file.  Usage: tools/gen_cities.py [zoneinfo_dir] [output.c]
"""
import os
import sys

# (display name, zone[, (lat, lon) override])
CITIES = [
    # Americas, US / Canada zones
    ("New York", "America/New_York"), ("Chicago", "America/Chicago"),
    ("Denver", "America/Denver"), ("Phoenix", "America/Phoenix"),
    ("Los Angeles", "America/Los_Angeles"), ("Anchorage", "America/Anchorage"),
    ("Honolulu", "Pacific/Honolulu"), ("Boise", "America/Boise"),
    ("Detroit", "America/Detroit"), ("Indianapolis", "America/Indiana/Indianapolis"),
    ("Louisville", "America/Kentucky/Louisville"), ("Juneau", "America/Juneau"),
    ("Columbus", "America/New_York", (39.96, -83.00)),
    ("Seattle", "America/Los_Angeles", (47.61, -122.33)),
    ("Miami", "America/New_York", (25.76, -80.19)),
    ("Atlanta", "America/New_York", (33.75, -84.39)),
    ("Boston", "America/New_York", (42.36, -71.06)),
    ("Dallas", "America/Chicago", (32.78, -96.80)),
    ("Houston", "America/Chicago", (29.76, -95.37)),
    ("San Francisco", "America/Los_Angeles", (37.77, -122.42)),
    ("Las Vegas", "America/Los_Angeles", (36.17, -115.14)),
    ("Washington", "America/New_York", (38.91, -77.04)),
    ("Toronto", "America/Toronto"), ("Vancouver", "America/Vancouver"),
    ("Edmonton", "America/Edmonton"), ("Winnipeg", "America/Winnipeg"),
    ("Halifax", "America/Halifax"), ("St. John's", "America/St_Johns"),
    ("Regina", "America/Regina"), ("Whitehorse", "America/Whitehorse"),
    ("Montreal", "America/Toronto", (45.50, -73.57)),
    ("Mexico City", "America/Mexico_City"), ("Tijuana", "America/Tijuana"),
    ("Monterrey", "America/Monterrey"), ("Cancun", "America/Cancun"),
    ("Havana", "America/Havana"), ("Kingston", "America/Jamaica"),
    ("Nassau", "America/Nassau"), ("Panama", "America/Panama"),
    ("San Jose", "America/Costa_Rica"), ("Guatemala", "America/Guatemala"),
    ("San Salvador", "America/El_Salvador"), ("Tegucigalpa", "America/Tegucigalpa"),
    ("Managua", "America/Managua"), ("Santo Domingo", "America/Santo_Domingo"),
    ("Port-au-Prince", "America/Port-au-Prince"), ("San Juan", "America/Puerto_Rico"),
    ("Bogota", "America/Bogota"), ("Lima", "America/Lima"),
    ("Quito", "America/Guayaquil", (-0.18, -78.47)),
    ("Caracas", "America/Caracas"), ("La Paz", "America/La_Paz"),
    ("Santiago", "America/Santiago"), ("Asuncion", "America/Asuncion"),
    ("Montevideo", "America/Montevideo"),
    ("Buenos Aires", "America/Argentina/Buenos_Aires"),
    ("Sao Paulo", "America/Sao_Paulo"), ("Manaus", "America/Manaus"),
    ("Rio de Janeiro", "America/Sao_Paulo", (-22.91, -43.17)),
    ("Brasilia", "America/Sao_Paulo", (-15.79, -47.88)),
    ("Georgetown", "America/Guyana"), ("Paramaribo", "America/Paramaribo"),
    ("Nuuk", "America/Nuuk"), ("Reykjavik", "Atlantic/Reykjavik"),
    ("Azores", "Atlantic/Azores"), ("Bermuda", "Atlantic/Bermuda"),
    # Europe
    ("London", "Europe/London"), ("Dublin", "Europe/Dublin"),
    ("Lisbon", "Europe/Lisbon"), ("Madrid", "Europe/Madrid"),
    ("Paris", "Europe/Paris"), ("Brussels", "Europe/Brussels"),
    ("Amsterdam", "Europe/Amsterdam"), ("Berlin", "Europe/Berlin"),
    ("Zurich", "Europe/Zurich"), ("Vienna", "Europe/Vienna"),
    ("Rome", "Europe/Rome"), ("Prague", "Europe/Prague"),
    ("Warsaw", "Europe/Warsaw"), ("Budapest", "Europe/Budapest"),
    ("Copenhagen", "Europe/Copenhagen"), ("Stockholm", "Europe/Stockholm"),
    ("Oslo", "Europe/Oslo"), ("Helsinki", "Europe/Helsinki"),
    ("Tallinn", "Europe/Tallinn"), ("Riga", "Europe/Riga"),
    ("Vilnius", "Europe/Vilnius"), ("Athens", "Europe/Athens"),
    ("Bucharest", "Europe/Bucharest"), ("Sofia", "Europe/Sofia"),
    ("Belgrade", "Europe/Belgrade"), ("Zagreb", "Europe/Zagreb"),
    ("Kyiv", "Europe/Kyiv"), ("Istanbul", "Europe/Istanbul"),
    ("Moscow", "Europe/Moscow"), ("Minsk", "Europe/Minsk"),
    ("Luxembourg", "Europe/Luxembourg"), ("Valletta", "Europe/Malta"),
    ("Monaco", "Europe/Monaco"), ("Chisinau", "Europe/Chisinau"),
    ("Sarajevo", "Europe/Sarajevo"), ("Skopje", "Europe/Skopje"),
    ("Tirana", "Europe/Tirane"), ("Bratislava", "Europe/Bratislava"),
    ("Ljubljana", "Europe/Ljubljana"),
    ("Longyearbyen", "Arctic/Longyearbyen", (78.22, 15.65)),   # polar day/night (R48/C11)
    # Africa
    ("Cairo", "Africa/Cairo"), ("Casablanca", "Africa/Casablanca"),
    ("Algiers", "Africa/Algiers"), ("Tunis", "Africa/Tunis"),
    ("Tripoli", "Africa/Tripoli"), ("Lagos", "Africa/Lagos"),
    ("Accra", "Africa/Accra"), ("Dakar", "Africa/Dakar"),
    ("Abidjan", "Africa/Abidjan"), ("Nairobi", "Africa/Nairobi"),
    ("Addis Ababa", "Africa/Addis_Ababa"), ("Dar es Salaam", "Africa/Dar_es_Salaam"),
    ("Kampala", "Africa/Kampala"), ("Khartoum", "Africa/Khartoum"),
    ("Kinshasa", "Africa/Kinshasa"), ("Luanda", "Africa/Luanda"),
    ("Harare", "Africa/Harare"), ("Johannesburg", "Africa/Johannesburg"),
    ("Maputo", "Africa/Maputo"), ("Windhoek", "Africa/Windhoek"),
    ("Antananarivo", "Indian/Antananarivo"), ("Mauritius", "Indian/Mauritius"),
    # Asia
    ("Jerusalem", "Asia/Jerusalem"), ("Beirut", "Asia/Beirut"),
    ("Amman", "Asia/Amman"), ("Damascus", "Asia/Damascus"),
    ("Baghdad", "Asia/Baghdad"), ("Riyadh", "Asia/Riyadh"),
    ("Kuwait", "Asia/Kuwait"), ("Doha", "Asia/Qatar"),
    ("Dubai", "Asia/Dubai"), ("Tehran", "Asia/Tehran"),
    ("Baku", "Asia/Baku"), ("Tbilisi", "Asia/Tbilisi"),
    ("Yerevan", "Asia/Yerevan"), ("Kabul", "Asia/Kabul"),
    ("Tashkent", "Asia/Tashkent"), ("Almaty", "Asia/Almaty"),
    ("Karachi", "Asia/Karachi"), ("Delhi", "Asia/Kolkata", (28.61, 77.21)),
    ("Mumbai", "Asia/Kolkata", (19.08, 72.88)),
    ("Kolkata", "Asia/Kolkata"), ("Colombo", "Asia/Colombo"),
    ("Kathmandu", "Asia/Kathmandu"), ("Dhaka", "Asia/Dhaka"),
    ("Yangon", "Asia/Yangon"), ("Bangkok", "Asia/Bangkok"),
    ("Ho Chi Minh City", "Asia/Ho_Chi_Minh"), ("Phnom Penh", "Asia/Phnom_Penh"),
    ("Vientiane", "Asia/Vientiane"), ("Jakarta", "Asia/Jakarta"),
    ("Singapore", "Asia/Singapore"), ("Kuala Lumpur", "Asia/Kuala_Lumpur"),
    ("Manila", "Asia/Manila"), ("Hong Kong", "Asia/Hong_Kong"),
    ("Taipei", "Asia/Taipei"), ("Shanghai", "Asia/Shanghai"),
    ("Beijing", "Asia/Shanghai", (39.90, 116.41)),
    ("Seoul", "Asia/Seoul"), ("Pyongyang", "Asia/Pyongyang"),
    ("Tokyo", "Asia/Tokyo"), ("Ulaanbaatar", "Asia/Ulaanbaatar"),
    ("Vladivostok", "Asia/Vladivostok"), ("Novosibirsk", "Asia/Novosibirsk"),
    ("Yekaterinburg", "Asia/Yekaterinburg"),
    # Oceania
    ("Perth", "Australia/Perth"), ("Darwin", "Australia/Darwin"),
    ("Adelaide", "Australia/Adelaide"), ("Brisbane", "Australia/Brisbane"),
    ("Sydney", "Australia/Sydney"), ("Melbourne", "Australia/Melbourne"),
    ("Hobart", "Australia/Hobart"), ("Auckland", "Pacific/Auckland"),
    ("Fiji", "Pacific/Fiji"), ("Port Moresby", "Pacific/Port_Moresby"),
    ("Noumea", "Pacific/Noumea"), ("Tahiti", "Pacific/Tahiti"),
    ("Apia", "Pacific/Apia"), ("Tongatapu", "Pacific/Tongatapu"),
    ("Guam", "Pacific/Guam"),
]


def parse_coord(s):
    """ISO 6709 +-DDMM[SS]+-DDDMM[SS] -> (lat, lon) degrees."""
    i = max(s.rfind("+"), s.rfind("-"))
    def conv(t, deg_digits):
        sign = -1.0 if t[0] == "-" else 1.0
        d = t[1:]
        deg = int(d[:deg_digits])
        mn = int(d[deg_digits:deg_digits + 2])
        sec = int(d[deg_digits + 2:deg_digits + 4] or 0)
        return sign * (deg + mn / 60.0 + sec / 3600.0)
    return conv(s[:i], 2), conv(s[i:], 3)


def load_tab(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.rstrip("\n").split("\t")
            out[p[2]] = (p[0].split(",")[0], parse_coord(p[1]))
    return out


def posix_tz(zoneinfo, zone):
    with open(os.path.join(zoneinfo, zone), "rb") as f:
        data = f.read()
    lines = data.rstrip(b"\n").split(b"\n")
    tz = lines[-1].decode("ascii")
    if not tz or data[:4] != b"TZif":
        raise SystemExit("no POSIX TZ footer in " + zone)
    return tz


# iso3166.tab names that read oddly on a watch, or are not plain ASCII (the fonts carry ASCII only)
COUNTRY_OVERRIDES = {
    "GB": "United Kingdom", "KR": "South Korea", "KP": "North Korea", "CD": "DR Congo",
    "MM": "Myanmar", "WS": "Samoa", "CI": "Ivory Coast",
}


def load_countries(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            code, name = line.rstrip("\n").split("\t")[:2]
            out[code] = COUNTRY_OVERRIDES.get(code, name)
    return out


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    zoneinfo = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/zoneinfo"
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        here, "..", "firmware", "components", "omni_core", "cities.c")

    z1970 = load_tab(os.path.join(zoneinfo, "zone1970.tab"))
    zall = load_tab(os.path.join(zoneinfo, "zone.tab"))

    rows = []
    for ent in CITIES:
        name, zone = ent[0], ent[1]
        country = zall.get(zone, z1970.get(zone, (None, None)))[0]
        coords = ent[2] if len(ent) > 2 else (z1970.get(zone) or zall.get(zone, (None, None)))[1]
        if country is None or coords is None:
            raise SystemExit("zone not found in tab files: " + zone)
        rows.append((name, country, zone, posix_tz(zoneinfo, zone), coords[0], coords[1]))

    # Names must be unique; disambiguate by country only if duplicates occur.
    counts = {}
    for r in rows:
        counts[r[0]] = counts.get(r[0], 0) + 1
    rows = [(f"{r[0]} ({r[1]})" if counts[r[0]] > 1 else r[0],) + r[1:] for r in rows]
    names = [r[0] for r in rows]
    if len(set(names)) != len(names):
        raise SystemExit("duplicate city names after disambiguation")
    rows.sort(key=lambda r: r[0])

    names_by_code = load_countries(os.path.join(zoneinfo, "iso3166.tab"))
    countries = sorted({r[1] for r in rows})
    for code in countries:
        n = names_by_code.get(code)
        if n is None or not all(0x20 <= ord(ch) < 0x7f for ch in n):
            raise SystemExit("no plain-ASCII country name for " + code + " (add a COUNTRY_OVERRIDES entry)")

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* generated by tools/gen_cities.py -- do not edit */\n")
        f.write('#include "omni/cities.h"\n#include <string.h>\n\n')
        f.write("static const omni_city_t s_cities[] = {\n")
        for n, c, z, p, la, lo in rows:
            f.write(f"    {{ {c_str(n)}, {c_str(c)}, {c_str(z)}, {c_str(p)}, {la:.2f}f, {lo:.2f}f }},\n")
        f.write("};\n\n")
        f.write("size_t omni_city_count(void)\n{\n    return sizeof s_cities / sizeof s_cities[0];\n}\n\n")
        f.write("const omni_city_t *omni_city_at(size_t i)\n{\n    return i < omni_city_count() ? &s_cities[i] : NULL;\n}\n\n")
        f.write("int omni_city_find(const char *name)\n{\n    for (size_t i = 0; i < omni_city_count(); i++)\n"
                "        if (strcmp(s_cities[i].name, name) == 0) return (int)i;\n    return -1;\n}\n")
        f.write("\nstatic const struct { char code[3]; const char *name; } s_countries[] = {\n")
        for code in countries:
            f.write(f"    {{ {c_str(code)}, {c_str(names_by_code[code])} }},\n")
        f.write("};\n\n")
        f.write("const char *omni_country_name(const char *code)\n{\n"
                "    for (size_t i = 0; i < sizeof s_countries / sizeof s_countries[0]; i++)\n"
                "        if (strcmp(s_countries[i].code, code) == 0) return s_countries[i].name;\n"
                "    return code;\n}\n")


if __name__ == "__main__":
    main()
