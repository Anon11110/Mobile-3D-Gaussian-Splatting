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

	[[nodiscard]] std::future<container::shared_ptr<SplatSoA>> Load(const container::filesystem::path &path);

	SplatLoader(const SplatLoader &)            = delete;
	SplatLoader &operator=(const SplatLoader &) = delete;

  private:
	class Impl;
	container::unique_ptr<Impl> pimpl;
};

}        // namespace msplat::engine