#include "gbpch.h"

#include "Gambit/RHI/GraphicsDevice.h"

#include <nvrhi/utils.h>

#include <exception>

namespace Gambit
{
	namespace
	{
		// How the texels of a readable texture format map onto an Image.
		struct ReadbackLayout
		{
			ImageFormat Format;
			uint32_t Channels;        // stored channels, 1 to 4
			uint32_t BytesPerChannel; // 1, 2 or 4
			bool Bgra;                // stored blue first
		};

		std::optional<ReadbackLayout> GetReadbackLayout(nvrhi::Format format)
		{
			switch (format)
			{
				case nvrhi::Format::R8_UNORM:
					return ReadbackLayout{ ImageFormat::RGBA8, 1, 1, false };
				case nvrhi::Format::RG8_UNORM:
					return ReadbackLayout{ ImageFormat::RGBA8, 2, 1, false };
				case nvrhi::Format::RGBA8_UNORM:
				case nvrhi::Format::SRGBA8_UNORM:
					return ReadbackLayout{ ImageFormat::RGBA8, 4, 1, false };
				case nvrhi::Format::BGRA8_UNORM:
				case nvrhi::Format::SBGRA8_UNORM:
					return ReadbackLayout{ ImageFormat::RGBA8, 4, 1, true };
				case nvrhi::Format::R16_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA16F, 1, 2, false };
				case nvrhi::Format::RG16_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA16F, 2, 2, false };
				case nvrhi::Format::RGBA16_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA16F, 4, 2, false };
				case nvrhi::Format::R32_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA32F, 1, 4, false };
				case nvrhi::Format::RG32_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA32F, 2, 4, false };
				case nvrhi::Format::RGBA32_FLOAT:
					return ReadbackLayout{ ImageFormat::RGBA32F, 4, 4, false };
				default:
					return std::nullopt;
			}
		}

		// Copies one row of texels into RGBA pixels, filling missing channels with 0 and alpha with 1.
		void
		ExpandRow(const ReadbackLayout& layout, const std::byte* source, std::span<std::byte> target, uint32_t width)
		{
			const uint32_t channelBytes = layout.BytesPerChannel;
			std::array<std::byte, 4> one{}; // the bytes of 1.0 in the channel type
			switch (channelBytes)
			{
				case 1:
					one[0] = std::byte{ 0xFF };
					break;
				case 2:
				{
					const uint16_t half = 0x3C00; // 1.0 as a half float
					std::memcpy(one.data(), &half, sizeof(half));
					break;
				}
				default:
				{
					const float value = 1.0f;
					std::memcpy(one.data(), &value, sizeof(value));
					break;
				}
			}

			for (uint32_t x = 0; x < width; ++x)
			{
				const std::byte* texel = source + size_t(x) * layout.Channels * channelBytes;
				std::byte* pixel = target.data() + size_t(x) * 4 * channelBytes;
				for (uint32_t c = 0; c < 4; ++c)
				{
					std::byte* destination = pixel + size_t(c) * channelBytes;
					if (c < layout.Channels)
					{
						const uint32_t sourceChannel = layout.Bgra && c < 3 ? 2 - c : c;
						std::memcpy(destination, texel + size_t(sourceChannel) * channelBytes, channelBytes);
					}
					else if (c == 3)
					{
						std::memcpy(destination, one.data(), channelBytes);
					}
					else
					{
						std::memset(destination, 0, channelBytes);
					}
				}
			}
		}

		// Waits until everything submitted to the graphics queue so far has finished.
		void WaitForGraphicsQueue(nvrhi::IDevice& device)
		{
			const nvrhi::EventQueryHandle query = device.createEventQuery();
			device.setEventQuery(query, nvrhi::CommandQueue::Graphics);
			device.waitEventQuery(query);
		}

		// Copies one mip level (of size `size`) of one slice of `texture` into an Image through a staging texture,
		// waiting for the GPU. Errors say what failed, without naming the texture.
		Result<Image> CopyToImage(
			nvrhi::IDevice& device, nvrhi::ITexture* texture, const ReadbackLayout& layout, uint32_t mipLevel,
			uint32_t arraySlice, glm::uvec2 size)
		{
			const nvrhi::TextureDesc stagingDesc = nvrhi::TextureDesc()
													   .setWidth(size.x)
													   .setHeight(size.y)
													   .setFormat(texture->getDesc().format)
													   .setDebugName("Readback staging texture");
			const nvrhi::StagingTextureHandle staging =
				device.createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
			const nvrhi::CommandListHandle commandList = device.createCommandList();
			if (!staging || !commandList)
			{
				return Error("NVRHI could not create the staging resources");
			}

			const nvrhi::TextureSlice source = nvrhi::TextureSlice()
												   .setMipLevel(mipLevel)
												   .setArraySlice(arraySlice)
												   .setWidth(size.x)
												   .setHeight(size.y)
												   .setDepth(1);
			const nvrhi::TextureSlice destination =
				nvrhi::TextureSlice().setWidth(size.x).setHeight(size.y).setDepth(1);
			commandList->open();
			commandList->copyTexture(staging, destination, texture, source);
			commandList->close();
			device.executeCommandList(commandList);
			WaitForGraphicsQueue(device);

			size_t rowPitch = 0;
			const auto* mapped = static_cast<const std::byte*>(
				device.mapStagingTexture(staging, destination, nvrhi::CpuAccessMode::Read, &rowPitch));
			if (mapped == nullptr)
			{
				return Error("the staging texture cannot be mapped");
			}
			Image image(size.x, size.y, layout.Format);
			for (uint32_t y = 0; y < size.y; ++y)
			{
				ExpandRow(layout, mapped + y * rowPitch, image.GetRow(y), size.x);
			}
			device.unmapStagingTexture(staging);
			return image;
		}

		// Copies `size` bytes from `offset` of `buffer` to the CPU through a staging buffer, waiting for the GPU.
		// Errors say what failed, without naming the buffer.
		Result<Buffer> CopyToBuffer(nvrhi::IDevice& device, nvrhi::IBuffer* buffer, uint64_t offset, uint64_t size)
		{
			const nvrhi::BufferDesc stagingDesc = nvrhi::BufferDesc()
													  .setByteSize(size)
													  .setCpuAccess(nvrhi::CpuAccessMode::Read)
													  .setInitialState(nvrhi::ResourceStates::CopyDest)
													  .setKeepInitialState(true)
													  .setDebugName("Readback staging buffer");
			const nvrhi::BufferHandle staging = device.createBuffer(stagingDesc);
			const nvrhi::CommandListHandle commandList = device.createCommandList();
			if (!staging || !commandList)
			{
				return Error("NVRHI could not create the staging resources");
			}
			commandList->open();
			commandList->copyBuffer(staging, 0, buffer, offset, size);
			commandList->close();
			device.executeCommandList(commandList);
			WaitForGraphicsQueue(device);

			const void* mapped = device.mapBuffer(staging, nvrhi::CpuAccessMode::Read);
			if (mapped == nullptr)
			{
				return Error("the staging buffer cannot be mapped");
			}
			Buffer bytes = Buffer::Copy(mapped, static_cast<size_t>(size));
			device.unmapBuffer(staging);
			return bytes;
		}
	}

	std::string_view ToString(GpuType type) noexcept
	{
		switch (type)
		{
			case GpuType::Other:
				return "other";
			case GpuType::Integrated:
				return "integrated";
			case GpuType::Discrete:
				return "discrete";
			case GpuType::Virtual:
				return "virtual";
			case GpuType::Cpu:
				return "CPU";
		}
		return "other";
	}

	Result<Image> GraphicsDevice::ReadbackTexture(nvrhi::ITexture* texture, uint32_t mipLevel, uint32_t arraySlice)
	{
		if (texture == nullptr)
		{
			return Error("cannot read back a null texture");
		}
		const nvrhi::TextureDesc& desc = texture->getDesc();
		const std::string& name = desc.debugName;
		if (desc.dimension != nvrhi::TextureDimension::Texture2D
		    && desc.dimension != nvrhi::TextureDimension::Texture2DArray
		    && desc.dimension != nvrhi::TextureDimension::TextureCube
		    && desc.dimension != nvrhi::TextureDimension::TextureCubeArray)
		{
			return Error(std::format(
				"cannot read back texture '{}': only 2D, 2D array and cube textures are supported, not {}", name,
				nvrhi::utils::TextureDimensionToString(desc.dimension)));
		}
		if (desc.sampleCount > 1)
		{
			return Error(std::format("cannot read back texture '{}': it is multisampled (resolve it first)", name));
		}
		if (mipLevel >= desc.mipLevels || arraySlice >= desc.arraySize)
		{
			return Error(std::format(
				"cannot read back mip {} of slice {} of texture '{}': it has {} mips and {} slices", mipLevel,
				arraySlice, name, desc.mipLevels, desc.arraySize));
		}
		const std::optional<ReadbackLayout> layout = GetReadbackLayout(desc.format);
		if (!layout)
		{
			return Error(std::format(
				"cannot read back texture '{}': format {} is not supported", name,
				nvrhi::utils::FormatToString(desc.format)));
		}

		const uint32_t width = std::max(desc.width >> mipLevel, 1u);
		const uint32_t height = std::max(desc.height >> mipLevel, 1u);
		if (width > Image::MaxDimension || height > Image::MaxDimension)
		{
			return Error(std::format(
				"cannot read back mip {} of texture '{}': {}x{} is larger than an Image can be ({} in each direction)",
				mipLevel, name, width, height, Image::MaxDimension));
		}
		// NVRHI's waits report a lost device with Vulkan-Hpp exceptions.
		Result<Image> image = [&]() -> Result<Image> {
			try
			{
				return CopyToImage(
					*GetNvrhiDevice(), texture, *layout, mipLevel, arraySlice, glm::uvec2(width, height));
			}
			catch (const std::exception& error)
			{
				return Error(error.what());
			}
		}();
		if (!image)
		{
			return Error(std::format("cannot read back texture '{}': {}", name, image.GetError().Message));
		}
		return image;
	}

	Result<Buffer> GraphicsDevice::ReadbackBuffer(nvrhi::IBuffer* buffer, uint64_t offset, uint64_t size)
	{
		if (buffer == nullptr)
		{
			return Error("cannot read back a null buffer");
		}
		const nvrhi::BufferDesc& desc = buffer->getDesc();
		if (size == 0 || offset > desc.byteSize || size > desc.byteSize - offset)
		{
			return Error(std::format(
				"cannot read back bytes [{}, +{}) of buffer '{}': it has {} bytes", offset, size, desc.debugName,
				desc.byteSize));
		}

		// NVRHI's waits report a lost device with Vulkan-Hpp exceptions.
		Result<Buffer> bytes = [&]() -> Result<Buffer> {
			try
			{
				return CopyToBuffer(*GetNvrhiDevice(), buffer, offset, size);
			}
			catch (const std::exception& error)
			{
				return Error(error.what());
			}
		}();
		if (!bytes)
		{
			return Error(std::format("cannot read back buffer '{}': {}", desc.debugName, bytes.GetError().Message));
		}
		return bytes;
	}
}
