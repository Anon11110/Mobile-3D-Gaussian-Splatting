#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/string.h>

namespace msplat::container
{

// Explicit instantiation helpers to ensure consistent type resolution across translation units
// These functions help MSVC properly resolve the filesystem namespace alias

// Helper function to ensure filesystem operations are consistently available
[[nodiscard]] bool exists(const filesystem::path &path)
{
	return filesystem::exists(path);
}

[[nodiscard]] bool is_directory(const filesystem::path &path)
{
	return filesystem::is_directory(path);
}

[[nodiscard]] bool is_regular_file(const filesystem::path &path)
{
	return filesystem::is_regular_file(path);
}

[[nodiscard]] filesystem::path current_path()
{
	return filesystem::current_path();
}

[[nodiscard]] filesystem::path canonical(const filesystem::path &path)
{
	return filesystem::canonical(path);
}

[[nodiscard]] filesystem::path canonical(const filesystem::path &path, std::error_code &ec)
{
	return filesystem::canonical(path, ec);
}

[[nodiscard]] filesystem::path weakly_canonical(const filesystem::path &path)
{
	return filesystem::weakly_canonical(path);
}

// Directory iteration helpers
[[nodiscard]] filesystem::directory_iterator directory_iterator(const filesystem::path &path)
{
	return filesystem::directory_iterator(path);
}

[[nodiscard]] filesystem::directory_iterator directory_iterator(const filesystem::path &path, std::error_code &ec)
{
	return filesystem::directory_iterator(path, ec);
}

}        // namespace msplat::container