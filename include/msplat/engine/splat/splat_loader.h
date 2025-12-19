#pragma once

#include <future>
#include <memory>
#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/memory.h>
#include <msplat/engine/splat/splat_soa.h>

namespace msplat::engine
{

class SplatLoader
{
  public:
	SplatLoader();
	~SplatLoader();

	SplatLoader(const SplatLoader &)                = delete;
	SplatLoader &operator=(const SplatLoader &)     = delete;
	SplatLoader(SplatLoader &&) noexcept            = default;
	SplatLoader &operator=(SplatLoader &&) noexcept = default;

	[[nodiscard]] std::future<container::shared_ptr<SplatSoA>> Load(const container::filesystem::path &path);

  private:
	class Impl;
	container::unique_ptr<Impl> pimpl;
};

}        // namespace msplat::engine