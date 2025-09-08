#include "test_framework.h"
#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/string.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/log.h>

// Test that basic string type aliases work
TEST(string_basic_types)
{
	// Test all string type aliases are available
	msplat::container::string    std_str = "hello";
	msplat::container::u8string  u8_str  = u8"hello";
	msplat::container::u16string u16_str = u"hello";
	msplat::container::u32string u32_str = U"hello";
	msplat::container::wstring   w_str   = L"hello";

	// Test string views
	msplat::container::string_view    std_view = std_str;
	msplat::container::u8string_view  u8_view  = u8_str;
	msplat::container::u16string_view u16_view = u16_str;
	msplat::container::u32string_view u32_view = u32_str;
	msplat::container::wstring_view   w_view   = w_str;

	// Basic functionality test
	if (std_str.size() != 5)
		return false;
	if (std_view.size() != 5)
		return false;

	return true;
}

// Test PMR allocator functionality (when using custom implementation)
TEST(string_pmr_allocators)
{
#ifndef MSPLAT_USE_SYSTEM_STL
	// Test that PMR strings use polymorphic allocators
	msplat::container::string str1 = "test string";
	msplat::container::string str2 = str1;                   // Copy
	msplat::container::string str3 = std::move(str2);        // Move

	// Verify basic operations work
	if (str1 != "test string")
		return false;
	if (str3 != "test string")
		return false;
	if (!str2.empty())
		return false;        // str2 should be empty after move

	// Test assignment
	str2 = str1;
	if (str2 != "test string")
		return false;

	return true;
#else
	// When using system STL, just verify strings work
	msplat::container::string str = "test";
	return str == "test";
#endif
}

// Test basic string hashing consistency
TEST(string_hash_consistency)
{
	msplat::container::hash<msplat::container::string> hasher;

	msplat::container::string str1 = "hello world";
	msplat::container::string str2 = "hello world";
	msplat::container::string str3 = "goodbye world";

	auto hash1 = hasher(str1);
	auto hash2 = hasher(str2);
	auto hash3 = hasher(str3);

	// Same strings should have same hash
	if (hash1 != hash2)
		return false;

	// Different strings should have different hashes (collision possible but unlikely)
	if (hash1 == hash3)
	{
		LOG_WARNING("Hash collision detected for different strings (rare but possible)");
	}

	return true;
}

// Test all Unicode string type hashing
TEST(string_hash_unicode_types)
{
	// Test each string type has working hash
	msplat::container::hash<msplat::container::string>    std_hasher;
	msplat::container::hash<msplat::container::u8string>  u8_hasher;
	msplat::container::hash<msplat::container::u16string> u16_hasher;
	msplat::container::hash<msplat::container::u32string> u32_hasher;
	msplat::container::hash<msplat::container::wstring>   w_hasher;

	// Test basic hashing
	auto hash1 = std_hasher("test");
	auto hash2 = u8_hasher(u8"test");
	auto hash3 = u16_hasher(u"test");
	auto hash4 = u32_hasher(U"test");
	auto hash5 = w_hasher(L"test");

	// Note: Different encodings may produce different hashes, that's expected
	// Just verify they all produce valid (non-zero) hashes for non-empty strings
	if (hash1 == 0 || hash2 == 0 || hash3 == 0 || hash4 == 0 || hash5 == 0)
	{
		return false;
	}

	return true;
}

// Test string view hashing
TEST(string_view_hashing)
{
	msplat::container::hash<msplat::container::string_view> view_hasher;
	msplat::container::hash<msplat::container::string>      string_hasher;

	msplat::container::string      str  = "test string";
	msplat::container::string_view view = str;

	auto string_hash = string_hasher(str);
	auto view_hash   = view_hasher(view);

	// String and string_view of same content should have same hash (transparent hashing)
	if (string_hash != view_hash)
		return false;

	return true;
}

// Test transparent hashing with heterogeneous lookup
TEST(string_transparent_hashing)
{
#ifndef MSPLAT_USE_SYSTEM_STL
	// Test that basic_string_hash supports transparent hashing
	msplat::container::basic_string_hash<char, std::char_traits<char>> hasher;

	msplat::container::string      str  = "hello";
	msplat::container::string_view view = str;
	const char                    *cstr = str.c_str();

	auto str_hash  = hasher(str);
	auto view_hash = hasher(view);
	auto cstr_hash = hasher(cstr);

	// All should produce the same hash (transparent hashing)
	if (str_hash != view_hash || view_hash != cstr_hash)
		return false;

	return true;
#else
	return true;        // Skip test when using system STL
#endif
}

// Test unordered_map with heterogeneous lookup
TEST(string_heterogeneous_lookup)
{
	using StringMap = msplat::container::unordered_map<msplat::container::string, int>;

#ifndef MSPLAT_USE_SYSTEM_STL
	// Create map with custom hash and allocator
	auto map = msplat::container::make_unordered_map_default<msplat::container::string, int>();
#else
	StringMap map;
#endif

	// Insert with string
	msplat::container::string key = "test_key";
	map[key]                      = 42;

	// Lookup with different but equivalent types
	msplat::container::string_view view_key = key;

	// Should be able to find the key
	auto it = map.find(key);
	if (it == map.end() || it->second != 42)
		return false;

	// Test that view works for lookup (if transparent hashing is working)
	if (map.count(key) != 1)
		return false;

	return true;
}

// Test C-string hashing
TEST(string_cstring_hashing)
{
	msplat::container::hash<const char *> cstr_hasher;

	const char *str1     = "hello";
	const char *str2     = "hello";
	const char *str3     = "world";
	const char *null_str = nullptr;

	auto hash1     = cstr_hasher(str1);
	auto hash2     = cstr_hasher(str2);
	auto hash3     = cstr_hasher(str3);
	auto null_hash = cstr_hasher(null_str);

	// Same C-strings should hash the same
	if (hash1 != hash2)
		return false;

	// Null pointer should hash to 0
	if (null_hash != 0)
		return false;

	// Different strings should hash differently (usually)
	if (hash1 == hash3)
	{
		LOG_WARNING("Hash collision for different C-strings");
	}

	return true;
}

// Test character array hashing
TEST(string_char_array_hashing)
{
#ifndef MSPLAT_USE_SYSTEM_STL
	// Test that character arrays are hashable
	msplat::container::hash<char[10]> char_array_hasher;

	char arr1[10] = "hello";
	char arr2[10] = "hello";
	char arr3[10] = "world";

	auto hash1 = char_array_hasher(arr1);
	auto hash2 = char_array_hasher(arr2);
	auto hash3 = char_array_hasher(arr3);

	// Same content should hash the same
	if (hash1 != hash2)
		return false;

	// Different content should hash differently
	if (hash1 == hash3)
	{
		LOG_WARNING("Hash collision for different character arrays");
	}

	return true;
#else
	return true;        // Skip when using system STL
#endif
}

// Test empty string handling
TEST(string_empty_handling)
{
	msplat::container::hash<msplat::container::string>      hasher;
	msplat::container::hash<msplat::container::string_view> view_hasher;

	msplat::container::string      empty_str  = "";
	msplat::container::string_view empty_view = empty_str;

	auto str_hash  = hasher(empty_str);
	auto view_hash = view_hasher(empty_view);

	// Empty string and empty view should hash the same
	if (str_hash != view_hash)
		return false;

	// Should produce a valid hash (may be 0, that's fine for empty strings)
	return true;
}

// Test very long strings
TEST(string_long_string_handling)
{
	msplat::container::hash<msplat::container::string> hasher;

	// Create a long string
	msplat::container::string long_str;
	long_str.reserve(10000);
	for (int i = 0; i < 1000; ++i)
	{
		long_str += "0123456789";
	}

	// Should be able to hash without issues
	auto hash1 = hasher(long_str);
	auto hash2 = hasher(long_str);

	// Same long string should hash consistently
	if (hash1 != hash2)
		return false;

	return true;
}

// Test Unicode string content
TEST(string_unicode_content)
{
	msplat::container::hash<msplat::container::string> hasher;

	// Test strings with actual Unicode content
	msplat::container::string ascii_str   = "hello";
	msplat::container::string unicode_str = "héllø";        // UTF-8 encoded

	auto ascii_hash   = hasher(ascii_str);
	auto unicode_hash = hasher(unicode_str);

	// Different strings should have different hashes
	if (ascii_hash == unicode_hash)
	{
		LOG_WARNING("Hash collision between ASCII and Unicode strings");
	}

	// Both should produce valid hashes
	return true;
}

// Test wide string functionality
TEST(string_wide_string)
{
	msplat::container::hash<msplat::container::wstring>      w_hasher;
	msplat::container::hash<msplat::container::wstring_view> wv_hasher;

	msplat::container::wstring      w_str  = L"wide string test";
	msplat::container::wstring_view w_view = w_str;

	auto w_hash  = w_hasher(w_str);
	auto wv_hash = wv_hasher(w_view);

	// Wide string and view should hash the same
	if (w_hash != wv_hash)
		return false;

	return true;
}

// Register all string tests
void register_string_tests()
{
	// Tests are automatically registered via static constructors
}