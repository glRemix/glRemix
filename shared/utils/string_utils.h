inline void utf8_to_wide(const char* utf8, WCHAR* wide, size_t wide_size)
{
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, static_cast<int>(wide_size));
}

static void wide_to_utf8(const WCHAR* wide, char* utf8, size_t utf8_size)
{
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, static_cast<int>(utf8_size), NULL, NULL);
}
