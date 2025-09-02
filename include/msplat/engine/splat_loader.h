#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <msplat/engine/splat_soa.h>

namespace msplat::engine
{

class SplatLoader
{
  public:
	SplatLoader();

	~SplatLoader();

	[[nodiscard]] std::future<std::shared_ptr<SplatSoA>> Load(const std::filesystem::path &path);

	SplatLoader(const SplatLoader &)            = delete;
	SplatLoader &operator=(const SplatLoader &) = delete;

  private:
	class Impl;
	std::unique_ptr<Impl> pimpl;
};

}        // namespace msplat::engine