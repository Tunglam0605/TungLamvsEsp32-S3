"""Workspace-only compatibility shim for ESP-IDF on Windows-1252 locales."""

import locale


_getlocale = locale.getlocale


def _getlocale_with_utf8_for_idf(category=locale.LC_CTYPE):
    language, encoding = _getlocale(category)
    normalized_encoding = encoding.lower().replace("-", "") if encoding else ""
    if category == locale.LC_CTYPE and normalized_encoding in {"1252", "cp1252", "windows1252"}:
        return language, "UTF-8"
    return language, encoding


locale.getlocale = _getlocale_with_utf8_for_idf
