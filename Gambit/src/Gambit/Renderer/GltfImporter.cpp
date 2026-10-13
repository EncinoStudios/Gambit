#include "gbpch.h"

#include "Gambit/Renderer/GltfImporter.h"

#include "Gambit/Core/FileSystem.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshUtils.h"
#include "Gambit/Renderer/Model.h"
#include "Gambit/Renderer/Texture.h"

#include <cgltf.h>

#include <cstdlib>
#include <limits>
#include <numbers>
#include <numeric>

namespace Gambit
{
	namespace
	{
		// Extensions a file may require: those the importer understands.
		constexpr std::array<std::string_view, 4> SupportedRequiredExtensions = {
			"KHR_mesh_quantization", "KHR_texture_transform", "KHR_materials_emissive_strength", "KHR_lights_punctual"
		};

		// Lumens of a point (or spot) light per candela of luminous intensity: 4 pi steradians.
		constexpr float LumensPerCandela = 4.0f * std::numbers::pi_v<float>;

		std::string_view Describe(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_success:
					return "success";
				case cgltf_result_data_too_short:
					return "the data is too short";
				case cgltf_result_unknown_format:
					return "unknown format (or an unsupported URI)";
				case cgltf_result_invalid_json:
					return "invalid JSON";
				case cgltf_result_invalid_gltf:
					return "invalid glTF";
				case cgltf_result_invalid_options:
					return "invalid options";
				case cgltf_result_file_not_found:
					return "a file was not found";
				case cgltf_result_io_error:
					return "an I/O error";
				case cgltf_result_out_of_memory:
					return "out of memory";
				case cgltf_result_legacy_gltf:
					return "glTF 1.0 is not supported";
				case cgltf_result_max_enum:
					break;
			}
			return "unknown error";
		}

		// cgltf's file callbacks, reading through the VFS.
		struct FileReader
		{
			const VFS& Vfs;
			std::string Failure; // why the last read failed
		};

		cgltf_result ReadFile(
			const cgltf_memory_options*, const cgltf_file_options* options, const char* path, cgltf_size* size,
			void** data)
		{
			FileReader& reader = *static_cast<FileReader*>(options->user_data);
			const Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized || normalized->empty() || FileSystem::IsAbsolutePath(*normalized))
			{
				reader.Failure = std::format("'{}' is not a path inside the project", path);
				return cgltf_result_file_not_found;
			}
			Result<Buffer> bytes = reader.Vfs.ReadBinary(*normalized);
			if (!bytes)
			{
				reader.Failure = bytes.GetError().Message;
				return cgltf_result_file_not_found;
			}
			if (*size != 0 && bytes->Size() < *size)
			{
				reader.Failure =
					std::format("'{}' has {} bytes, the glTF file expects {}", *normalized, bytes->Size(), *size);
				return cgltf_result_data_too_short;
			}
			void* copy = std::malloc(std::max<size_t>(bytes->Size(), 1));
			if (copy == nullptr)
			{
				return cgltf_result_out_of_memory;
			}
			std::memcpy(copy, bytes->Data(), bytes->Size());
			*size = bytes->Size();
			*data = copy;
			return cgltf_result_success;
		}

		void ReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data)
		{
			std::free(data);
		}

		struct DataDeleter
		{
			void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
		};
		using DataPointer = std::unique_ptr<cgltf_data, DataDeleter>;

		std::string_view NameOf(const char* name)
		{
			return name != nullptr ? std::string_view(name) : std::string_view();
		}

		// "<prefix>/<name>" for names that are present and unique, "<prefix>/#<index>" for the others.
		std::vector<std::string> MakeKeys(std::string_view prefix, const std::vector<std::string>& names)
		{
			std::map<std::string_view, size_t> counts;
			for (const std::string& name : names)
			{
				++counts[name];
			}
			std::vector<std::string> keys;
			keys.reserve(names.size());
			for (size_t index = 0; index < names.size(); ++index)
			{
				const std::string& name = names[index];
				keys.push_back(
					!name.empty() && counts[name] == 1 ? std::format("{}/{}", prefix, name)
													   : std::format("{}/#{}", prefix, index));
			}
			return keys;
		}

		// RFC 4648 base64 (the payload of a data URI); nullopt for malformed text.
		std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text)
		{
			const auto value = [](char character) -> int {
				if (character >= 'A' && character <= 'Z')
				{
					return character - 'A';
				}
				if (character >= 'a' && character <= 'z')
				{
					return character - 'a' + 26;
				}
				if (character >= '0' && character <= '9')
				{
					return character - '0' + 52;
				}
				if (character == '+')
				{
					return 62;
				}
				if (character == '/')
				{
					return 63;
				}
				return -1;
			};
			while (!text.empty() && text.back() == '=')
			{
				text.remove_suffix(1);
			}
			std::vector<std::byte> bytes;
			bytes.reserve(text.size() * 3 / 4);
			uint32_t buffer = 0;
			int bits = 0;
			for (const char character : text)
			{
				const int digit = value(character);
				if (digit < 0)
				{
					return std::nullopt;
				}
				buffer = (buffer << 6) | static_cast<uint32_t>(digit);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					bytes.push_back(static_cast<std::byte>((buffer >> bits) & 0xFF));
				}
			}
			return bytes;
		}

		struct TextureUse
		{
			bool Srgb = false;
			bool Linear = false;
		};

		// One import: the parsed document and everything made from it so far.
		class GltfImport
		{
		public:
			GltfImport(const AssetLoadContext& context, cgltf_data& data)
				: m_Context(context), m_Data(data)
			{}

			[[nodiscard]] Result<Ref<Asset>> Run()
			{
				ReportIgnoredFeatures();
				CollectTextureUses();
				m_TextureKeys = MakeKeys("Texture", CollectTextureNames());
				ImportMaterials();
				if (Result<void> meshes = ImportMeshes(); !meshes)
				{
					return meshes.GetError();
				}
				std::vector<ModelNode> nodes = ImportNodes();
				std::vector<SubAssetRef> subAssets;
				for (std::vector<SubAssetRef>* list : { &m_Textures, &m_Materials, &m_Meshes })
				{
					subAssets.insert(subAssets.end(), list->begin(), list->end());
				}
				return Ref<Asset>(CreateRef<Model>(std::move(nodes), std::move(subAssets)));
			}

		private:
			void Warn(std::string message) { m_Context.Report.Warn("", std::move(message)); }

			void ReportIgnoredFeatures()
			{
				if (m_Data.skins_count != 0 || m_Data.animations_count != 0)
				{
					Warn("skins and animations are not imported yet");
				}
				const bool morphTargets =
					std::ranges::any_of(std::span(m_Data.meshes, m_Data.meshes_count), [](const cgltf_mesh& mesh) {
						return std::ranges::any_of(
							std::span(mesh.primitives, mesh.primitives_count),
							[](const cgltf_primitive& primitive) { return primitive.targets_count != 0; });
					});
				if (morphTargets)
				{
					Warn("morph targets are not imported yet");
				}
				if (std::ranges::any_of(std::span(m_Data.nodes, m_Data.nodes_count), [](const cgltf_node& node) {
						return node.has_mesh_gpu_instancing != 0;
					}))
				{
					Warn("EXT_mesh_gpu_instancing is not supported: each node draws its mesh once");
				}
			}

			// ---- Textures ----

			void CollectTextureUses()
			{
				m_TextureUses.assign(m_Data.textures_count, TextureUse{});
				const auto use = [&](const cgltf_texture_view& view, bool srgb) {
					if (view.texture == nullptr)
					{
						return;
					}
					TextureUse& entry = m_TextureUses[cgltf_texture_index(&m_Data, view.texture)];
					(srgb ? entry.Srgb : entry.Linear) = true;
				};
				for (size_t index = 0; index < m_Data.materials_count; ++index)
				{
					const cgltf_material& material = m_Data.materials[index];
					use(material.pbr_metallic_roughness.base_color_texture, true);
					use(material.pbr_metallic_roughness.metallic_roughness_texture, false);
					use(material.normal_texture, false);
					use(material.occlusion_texture, false);
					use(material.emissive_texture, true);
				}
			}

			std::vector<std::string> CollectTextureNames() const
			{
				std::vector<std::string> names;
				for (size_t index = 0; index < m_Data.textures_count; ++index)
				{
					const cgltf_texture& texture = m_Data.textures[index];
					std::string_view name = NameOf(texture.name);
					if (name.empty() && texture.image != nullptr)
					{
						name = NameOf(texture.image->name);
						const std::string_view uri = NameOf(texture.image->uri);
						if (name.empty() && !uri.empty() && !uri.starts_with("data:"))
						{
							const size_t slash = uri.find_last_of('/');
							name = uri.substr(slash == std::string_view::npos ? 0 : slash + 1);
						}
					}
					names.emplace_back(name);
				}
				return names;
			}

			// The bytes of a glTF image: a buffer view (GLB), a data URI or a file next to the glTF file.
			Result<std::vector<std::byte>> ReadImageBytes(const cgltf_image& image) const
			{
				if (image.buffer_view != nullptr)
				{
					const uint8_t* bytes = cgltf_buffer_view_data(image.buffer_view);
					if (bytes == nullptr)
					{
						return Error("its buffer view has no data");
					}
					const auto* begin = reinterpret_cast<const std::byte*>(bytes);
					return std::vector<std::byte>(begin, begin + image.buffer_view->size);
				}
				const std::string_view uri = NameOf(image.uri);
				if (uri.empty())
				{
					return Error("it has neither a URI nor a buffer view");
				}
				if (uri.starts_with("data:"))
				{
					const size_t comma = uri.find(',');
					if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64"))
					{
						return Error("its data URI is not base64");
					}
					std::optional<std::vector<std::byte>> bytes = DecodeBase64(uri.substr(comma + 1));
					if (!bytes.has_value())
					{
						return Error("its data URI is not valid base64");
					}
					return std::move(*bytes);
				}
				if (uri.find("://") != std::string_view::npos)
				{
					return Error::Format("'{}' is not a relative file URI", uri);
				}
				std::string decoded(uri);
				decoded.resize(cgltf_decode_uri(decoded.data()));
				const std::string_view file = m_Context.Record.Path;
				const size_t slash = file.find_last_of('/');
				const std::string combined =
					slash == std::string_view::npos ? decoded : std::format("{}/{}", file.substr(0, slash), decoded);
				const Result<std::string> normalized = FileSystem::NormalizePath(combined);
				if (!normalized || normalized->empty() || FileSystem::IsAbsolutePath(*normalized))
				{
					return Error::Format("'{}' is not a path inside the project", uri);
				}
				Result<Buffer> bytes = m_Context.Vfs.ReadBinary(*normalized);
				if (!bytes)
				{
					return bytes.GetError();
				}
				const auto* begin = static_cast<const std::byte*>(bytes->Data());
				return std::vector<std::byte>(begin, begin + bytes->Size());
			}

			// The decoded image of a glTF image, decoded once; null when it cannot be decoded (warned once).
			const Image* GetImage(const cgltf_image& image)
			{
				const size_t index = cgltf_image_index(&m_Data, &image);
				const auto cached = m_Images.find(index);
				if (cached != m_Images.end())
				{
					return cached->second.has_value() ? &*cached->second : nullptr;
				}
				std::optional<Image>& slot = m_Images[index];
				Result<std::vector<std::byte>> bytes = ReadImageBytes(image);
				if (!bytes)
				{
					Warn(std::format("image {} cannot be read: {}", index, bytes.GetError().Message));
					return nullptr;
				}
				Result<Image> decoded = DecodeTextureImage(BufferView(bytes->data(), bytes->size()));
				if (!decoded)
				{
					Warn(std::format("image {} cannot be decoded: {}", index, decoded.GetError().Message));
					return nullptr;
				}
				slot = std::move(*decoded);
				return &*slot;
			}

			// The handle of the texture sub-asset for a texture used in the given color space; null when the texture
			// has no usable image.
			AssetHandle GetTexture(const cgltf_texture& texture, bool srgb)
			{
				const size_t index = cgltf_texture_index(&m_Data, &texture);
				const auto key = std::make_pair(index, srgb);
				if (const auto found = m_TextureHandles.find(key); found != m_TextureHandles.end())
				{
					return found->second;
				}
				AssetHandle& handle = m_TextureHandles[key];
				if (texture.image == nullptr)
				{
					Warn(std::format(
						"texture {} has no PNG or JPEG image (KTX2 and WebP are not supported); it is ignored", index));
					return handle;
				}
				const Image* image = GetImage(*texture.image);
				if (image == nullptr)
				{
					return handle;
				}
				TextureImportSettings settings;
				settings.Srgb = srgb;
				if (const cgltf_sampler* sampler = texture.sampler; sampler != nullptr)
				{
					settings.Filter = sampler->mag_filter == cgltf_filter_type_nearest ? TextureFilter::Nearest
																					   : TextureFilter::Linear;
					settings.GenerateMips = sampler->min_filter != cgltf_filter_type_nearest
						&& sampler->min_filter != cgltf_filter_type_linear;
					settings.WrapU = ToWrap(sampler->wrap_s);
					settings.WrapV = ToWrap(sampler->wrap_t);
				}
				// A texture used both ways: the sRGB one keeps the plain key.
				const TextureUse& use = m_TextureUses[index];
				const std::string subKey =
					!srgb && use.Srgb ? std::format("{} (Linear)", m_TextureKeys[index]) : m_TextureKeys[index];
				handle = m_Context.SubAssets.Acquire(subKey, AssetType::Texture);
				m_Textures.push_back({ handle, CreateRef<Texture2D>(*image, settings) });
				return handle;
			}

			static TextureWrap ToWrap(cgltf_wrap_mode mode)
			{
				switch (mode)
				{
					case cgltf_wrap_mode_clamp_to_edge:
						return TextureWrap::ClampToEdge;
					case cgltf_wrap_mode_mirrored_repeat:
						return TextureWrap::MirroredRepeat;
					case cgltf_wrap_mode_repeat:
						break;
				}
				return TextureWrap::Repeat;
			}

			// ---- Materials ----

			void ImportMaterials()
			{
				std::vector<std::string> names;
				for (size_t index = 0; index < m_Data.materials_count; ++index)
				{
					names.emplace_back(NameOf(m_Data.materials[index].name));
				}
				const std::vector<std::string> keys = MakeKeys("Material", names);
				m_MaterialHandles.resize(m_Data.materials_count);
				for (size_t index = 0; index < m_Data.materials_count; ++index)
				{
					MaterialData data = ConvertMaterial(m_Data.materials[index], index);
					const AssetHandle handle = m_Context.SubAssets.Acquire(keys[index], AssetType::Material);
					m_MaterialHandles[index] = handle;
					m_Materials.push_back({ handle, CreateRef<Material>(std::move(data)) });
				}
			}

			MaterialData ConvertMaterial(const cgltf_material& material, size_t index)
			{
				MaterialData data;
				// glTF's defaults apply when pbrMetallicRoughness is absent too (cgltf only sets them when present).
				data.BaseColor = glm::vec4(1.0f);
				data.Metallic = 1.0f;
				data.Roughness = 1.0f;
				if (material.has_pbr_metallic_roughness)
				{
					const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
					data.BaseColor = glm::make_vec4(pbr.base_color_factor);
					data.Metallic = pbr.metallic_factor;
					data.Roughness = pbr.roughness_factor;
					data.BaseColorTexture = UseTexture(pbr.base_color_texture, true, index);
					data.MetallicRoughnessTexture = UseTexture(pbr.metallic_roughness_texture, false, index);
				}
				else if (material.has_pbr_specular_glossiness)
				{
					Warn(std::format(
						"material {} uses KHR_materials_pbrSpecularGlossiness, which is not supported; it is imported "
						"with metallic-roughness defaults",
						index));
				}
				data.NormalTexture = UseTexture(material.normal_texture, false, index);
				data.NormalScale = material.normal_texture.texture != nullptr ? material.normal_texture.scale : 1.0f;
				data.OcclusionTexture = UseTexture(material.occlusion_texture, false, index);
				data.OcclusionStrength =
					material.occlusion_texture.texture != nullptr ? material.occlusion_texture.scale : 1.0f;
				data.EmissiveColor = glm::make_vec3(material.emissive_factor);
				data.EmissiveTexture = UseTexture(material.emissive_texture, true, index);
				data.EmissiveStrength =
					material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1.0f;
				switch (material.alpha_mode)
				{
					case cgltf_alpha_mode_opaque:
						data.Alpha = AlphaMode::Opaque;
						break;
					case cgltf_alpha_mode_mask:
						data.Alpha = AlphaMode::Mask;
						break;
					case cgltf_alpha_mode_blend:
						data.Alpha = AlphaMode::Blend;
						break;
					case cgltf_alpha_mode_max_enum:
						break;
				}
				data.AlphaCutoff = material.alpha_cutoff;
				data.DoubleSided = material.double_sided != 0;
				ApplyTextureTransform(material, data, index);
				return data;
			}

			AssetHandle UseTexture(const cgltf_texture_view& view, bool srgb, size_t materialIndex)
			{
				if (view.texture == nullptr)
				{
					return {};
				}
				const int texcoord =
					view.has_transform && view.transform.has_texcoord ? view.transform.texcoord : view.texcoord;
				if (texcoord != 0)
				{
					Warn(std::format(
						"material {} samples a texture with TEXCOORD_{}; every texture uses TEXCOORD_0", materialIndex,
						texcoord));
				}
				return GetTexture(*view.texture, srgb);
			}

			// KHR_texture_transform: the material has one UV transform; it is taken from the first textured slot
			// that has one, and the other textured slots should agree with it.
			void ApplyTextureTransform(const cgltf_material& material, MaterialData& data, size_t index)
			{
				const std::array<const cgltf_texture_view*, 5> views = {
					&material.pbr_metallic_roughness.base_color_texture,
					&material.pbr_metallic_roughness.metallic_roughness_texture, &material.normal_texture,
					&material.occlusion_texture, &material.emissive_texture
				};
				const cgltf_texture_transform* chosen = nullptr;
				for (const cgltf_texture_view* view : views)
				{
					if (view->texture != nullptr && view->has_transform)
					{
						chosen = &view->transform;
						break;
					}
				}
				if (chosen == nullptr)
				{
					return;
				}
				const cgltf_texture_transform identity{ { 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f }, 0, 0 };
				const bool differs = std::ranges::any_of(views, [&](const cgltf_texture_view* view) {
					if (view->texture == nullptr)
					{
						return false;
					}
					const cgltf_texture_transform& transform = view->has_transform ? view->transform : identity;
					return transform.offset[0] != chosen->offset[0] || transform.offset[1] != chosen->offset[1]
						|| transform.scale[0] != chosen->scale[0] || transform.scale[1] != chosen->scale[1]
						|| transform.rotation != chosen->rotation;
				});
				data.UVTiling = glm::make_vec2(chosen->scale);
				data.UVOffset = glm::make_vec2(chosen->offset);
				if (chosen->rotation != 0.0f)
				{
					Warn(std::format("material {}: the rotation of KHR_texture_transform is ignored", index));
				}
				if (differs)
				{
					Warn(std::format(
						"material {} has different texture transforms per texture; all use the first one", index));
				}
			}

			// ---- Meshes ----

			Result<void> ImportMeshes()
			{
				std::vector<std::string> names;
				for (size_t index = 0; index < m_Data.meshes_count; ++index)
				{
					names.emplace_back(NameOf(m_Data.meshes[index].name));
				}
				const std::vector<std::string> keys = MakeKeys("Mesh", names);
				m_MeshHandles.resize(m_Data.meshes_count);
				for (size_t index = 0; index < m_Data.meshes_count; ++index)
				{
					Result<std::optional<MeshData>> mesh = ConvertMesh(m_Data.meshes[index], index);
					if (!mesh)
					{
						return Error::Format("mesh {}: {}", index, mesh.GetError().Message);
					}
					if (!mesh->has_value())
					{
						continue;
					}
					const AssetHandle handle = m_Context.SubAssets.Acquire(keys[index], AssetType::Mesh);
					m_MeshHandles[index] = handle;
					m_Meshes.push_back({ handle, CreateRef<MeshSource>(std::move(**mesh)) });
				}
				return {};
			}

			// The mesh, or nullopt when it has no triangles (warned).
			Result<std::optional<MeshData>> ConvertMesh(const cgltf_mesh& gltfMesh, size_t meshIndex)
			{
				MeshData mesh;
				std::map<const cgltf_material*, uint32_t> slots;
				for (size_t index = 0; index < gltfMesh.primitives_count; ++index)
				{
					const cgltf_primitive& primitive = gltfMesh.primitives[index];
					Result<std::optional<MeshData>> converted = ConvertPrimitive(primitive);
					if (!converted)
					{
						return Error::Format("primitive {}: {}", index, converted.GetError().Message);
					}
					if (!converted->has_value())
					{
						Warn(std::format(
							"mesh {} primitive {}: only triangles are imported (points and lines are skipped)",
							meshIndex, index));
						continue;
					}
					const auto [slot, added] =
						slots.try_emplace(primitive.material, static_cast<uint32_t>(mesh.Materials.size()));
					if (added)
					{
						mesh.Materials.push_back(
							primitive.material != nullptr
								? m_MaterialHandles[cgltf_material_index(&m_Data, primitive.material)]
								: AssetHandle());
					}
					Append(mesh, **converted, slot->second, std::format("Primitive {}", index));
				}
				if (mesh.Submeshes.empty())
				{
					Warn(std::format("mesh {} has no triangles; it is not imported", meshIndex));
					return std::optional<MeshData>();
				}
				MeshUtils::ComputeBounds(mesh);
				if (Result<void> valid = mesh.Validate(); !valid)
				{
					return valid.GetError();
				}
				return std::optional<MeshData>(std::move(mesh));
			}

			// Appends one primitive (a single submesh) to the mesh; streams that only one of them has are filled with
			// neutral values for the other.
			static void Append(MeshData& mesh, MeshData& primitive, uint32_t materialSlot, std::string name)
			{
				const size_t vertexBase = mesh.Positions.size();
				const size_t vertexCount = primitive.Positions.size();
				const auto merge = [&](auto& to, auto& from, const auto& neutral) {
					if (to.empty() && from.empty())
					{
						return;
					}
					to.resize(vertexBase, neutral);
					if (from.empty())
					{
						to.resize(vertexBase + vertexCount, neutral);
					}
					else
					{
						to.insert(to.end(), from.begin(), from.end());
					}
				};
				mesh.Positions.insert(mesh.Positions.end(), primitive.Positions.begin(), primitive.Positions.end());
				merge(mesh.Normals, primitive.Normals, glm::vec3(0.0f, 1.0f, 0.0f));
				merge(mesh.Tangents, primitive.Tangents, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
				merge(mesh.UV0, primitive.UV0, glm::vec2(0.0f));
				merge(mesh.UV1, primitive.UV1, glm::vec2(0.0f));
				merge(mesh.Colors, primitive.Colors, glm::vec4(1.0f));
				merge(mesh.Joints, primitive.Joints, glm::u16vec4(0));
				merge(mesh.Weights, primitive.Weights, glm::vec4(0.0f));
				Submesh submesh;
				submesh.Name = std::move(name);
				submesh.BaseIndex = static_cast<uint32_t>(mesh.Indices.size());
				submesh.IndexCount = static_cast<uint32_t>(primitive.Indices.size());
				submesh.BaseVertex = static_cast<uint32_t>(vertexBase);
				submesh.VertexCount = static_cast<uint32_t>(vertexCount);
				submesh.MaterialSlot = materialSlot;
				for (const uint32_t index : primitive.Indices)
				{
					mesh.Indices.push_back(static_cast<uint32_t>(vertexBase) + index);
				}
				mesh.Submeshes.push_back(std::move(submesh));
			}

			template<typename T, size_t Components>
			static Result<std::vector<T>>
			ReadFloats(const cgltf_accessor& accessor, size_t vertexCount, std::string_view what)
			{
				const size_t components = cgltf_num_components(accessor.type);
				if (components != Components)
				{
					return Error::Format("{} has {} components, {} expected", what, components, Components);
				}
				if (accessor.count != vertexCount)
				{
					return Error::Format("{} has {} elements for {} vertices", what, accessor.count, vertexCount);
				}
				std::vector<T> values(vertexCount);
				if (vertexCount != 0
				    && cgltf_accessor_unpack_floats(&accessor, glm::value_ptr(values[0]), vertexCount * Components)
				        != vertexCount * Components)
				{
					return Error::Format("{} cannot be read", what);
				}
				return values;
			}

			// The primitive as a mesh of one submesh with normals and tangents; nullopt for points and lines.
			Result<std::optional<MeshData>> ConvertPrimitive(const cgltf_primitive& primitive)
			{
				if (primitive.type != cgltf_primitive_type_triangles
				    && primitive.type != cgltf_primitive_type_triangle_strip
				    && primitive.type != cgltf_primitive_type_triangle_fan)
				{
					return std::optional<MeshData>();
				}
				const cgltf_accessor* positions = nullptr;
				for (size_t index = 0; index < primitive.attributes_count; ++index)
				{
					if (primitive.attributes[index].type == cgltf_attribute_type_position)
					{
						positions = primitive.attributes[index].data;
					}
				}
				if (positions == nullptr)
				{
					return Error("it has no POSITION attribute");
				}
				const size_t vertexCount = positions->count;
				if (vertexCount == 0 || vertexCount > std::numeric_limits<uint32_t>::max())
				{
					return Error::Format("it has {} vertices", vertexCount);
				}

				MeshData mesh;
				for (size_t index = 0; index < primitive.attributes_count; ++index)
				{
					const cgltf_attribute& attribute = primitive.attributes[index];
					const cgltf_accessor& accessor = *attribute.data;
					const std::string what = std::format("attribute {}", NameOf(attribute.name));
					Result<void> read;
					switch (attribute.type)
					{
						case cgltf_attribute_type_position:
							read = Assign(mesh.Positions, ReadFloats<glm::vec3, 3>(accessor, vertexCount, what));
							break;
						case cgltf_attribute_type_normal:
							read = Assign(mesh.Normals, ReadFloats<glm::vec3, 3>(accessor, vertexCount, what));
							break;
						case cgltf_attribute_type_tangent:
							read = Assign(mesh.Tangents, ReadFloats<glm::vec4, 4>(accessor, vertexCount, what));
							break;
						case cgltf_attribute_type_texcoord:
							if (attribute.index == 0)
							{
								read = Assign(mesh.UV0, ReadFloats<glm::vec2, 2>(accessor, vertexCount, what));
							}
							else if (attribute.index == 1)
							{
								read = Assign(mesh.UV1, ReadFloats<glm::vec2, 2>(accessor, vertexCount, what));
							}
							break;
						case cgltf_attribute_type_color:
							if (attribute.index == 0)
							{
								read = ReadColors(accessor, vertexCount, what, mesh.Colors);
							}
							break;
						case cgltf_attribute_type_joints:
							if (attribute.index == 0)
							{
								read = ReadJoints(accessor, vertexCount, what, mesh.Joints);
							}
							else
							{
								m_ExtraInfluences = true;
							}
							break;
						case cgltf_attribute_type_weights:
							if (attribute.index == 0)
							{
								read = Assign(mesh.Weights, ReadFloats<glm::vec4, 4>(accessor, vertexCount, what));
							}
							break;
						case cgltf_attribute_type_invalid:
						case cgltf_attribute_type_custom:
						case cgltf_attribute_type_max_enum:
							break;
					}
					if (!read)
					{
						return read.GetError();
					}
				}
				if (mesh.Joints.empty() != mesh.Weights.empty())
				{
					mesh.Joints.clear();
					mesh.Weights.clear();
					Warn("a primitive has joints without weights (or the reverse); its skinning data is dropped");
				}
				for (glm::vec4& weights : mesh.Weights)
				{
					const float sum = weights.x + weights.y + weights.z + weights.w;
					if (sum > 0.0f)
					{
						weights /= sum;
					}
				}

				std::vector<uint32_t> indices;
				if (primitive.indices != nullptr)
				{
					indices.resize(primitive.indices->count);
					if (!indices.empty()
					    && cgltf_accessor_unpack_indices(
							   primitive.indices, indices.data(), sizeof(uint32_t), indices.size())
					        != indices.size())
					{
						return Error("its indices cannot be read");
					}
					if (std::ranges::any_of(indices, [&](uint32_t index) { return index >= vertexCount; }))
					{
						return Error("an index refers to a vertex that does not exist");
					}
				}
				else
				{
					indices.resize(vertexCount);
					std::iota(indices.begin(), indices.end(), 0u);
				}
				if (primitive.type == cgltf_primitive_type_triangle_strip)
				{
					indices = MeshUtils::TriangleStripToList(indices);
				}
				else if (primitive.type == cgltf_primitive_type_triangle_fan)
				{
					indices = MeshUtils::TriangleFanToList(indices);
				}
				else
				{
					indices.resize(indices.size() - indices.size() % 3);
				}
				if (indices.empty())
				{
					return std::optional<MeshData>();
				}
				mesh.Indices = std::move(indices);
				Submesh submesh;
				submesh.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
				submesh.VertexCount = static_cast<uint32_t>(vertexCount);
				mesh.Submeshes = { submesh };
				mesh.Materials = { AssetHandle() };

				if (mesh.Normals.empty())
				{
					MeshUtils::GenerateFlatNormals(mesh);
				}
				if (mesh.Tangents.empty())
				{
					if (Result<void> tangents = MeshUtils::GenerateTangents(mesh); !tangents)
					{
						return tangents.GetError();
					}
				}
				return std::optional<MeshData>(std::move(mesh));
			}

			template<typename T>
			static Result<void> Assign(std::vector<T>& target, Result<std::vector<T>> values)
			{
				if (!values)
				{
					return values.GetError();
				}
				target = std::move(*values);
				return {};
			}

			static Result<void> ReadColors(
				const cgltf_accessor& accessor, size_t vertexCount, std::string_view what,
				std::vector<glm::vec4>& colors)
			{
				if (accessor.type == cgltf_type_vec4)
				{
					return Assign(colors, ReadFloats<glm::vec4, 4>(accessor, vertexCount, what));
				}
				Result<std::vector<glm::vec3>> rgb = ReadFloats<glm::vec3, 3>(accessor, vertexCount, what);
				if (!rgb)
				{
					return rgb.GetError();
				}
				colors.resize(vertexCount);
				for (size_t index = 0; index < vertexCount; ++index)
				{
					colors[index] = glm::vec4((*rgb)[index], 1.0f);
				}
				return {};
			}

			static Result<void> ReadJoints(
				const cgltf_accessor& accessor, size_t vertexCount, std::string_view what,
				std::vector<glm::u16vec4>& joints)
			{
				if (accessor.type != cgltf_type_vec4 || accessor.count != vertexCount)
				{
					return Error::Format("{} must be a VEC4 per vertex", what);
				}
				joints.resize(vertexCount);
				for (size_t index = 0; index < vertexCount; ++index)
				{
					cgltf_uint values[4] = {};
					if (!cgltf_accessor_read_uint(&accessor, index, values, 4))
					{
						return Error::Format("{} cannot be read", what);
					}
					for (int component = 0; component < 4; ++component)
					{
						if (values[component] > std::numeric_limits<uint16_t>::max())
						{
							return Error::Format("{} has a joint index above 65535", what);
						}
						joints[index][component] = static_cast<uint16_t>(values[component]);
					}
				}
				return {};
			}

			// ---- Nodes ----

			std::vector<ModelNode> ImportNodes()
			{
				if (m_ExtraInfluences)
				{
					Warn("only the first four joint influences per vertex are imported");
				}
				std::vector<const cgltf_node*> roots;
				const cgltf_scene* scene =
					m_Data.scene != nullptr ? m_Data.scene : (m_Data.scenes_count != 0 ? &m_Data.scenes[0] : nullptr);
				if (scene != nullptr)
				{
					roots.assign(scene->nodes, scene->nodes + scene->nodes_count);
				}
				else
				{
					for (size_t index = 0; index < m_Data.nodes_count; ++index)
					{
						if (m_Data.nodes[index].parent == nullptr)
						{
							roots.push_back(&m_Data.nodes[index]);
						}
					}
				}

				std::vector<ModelNode> nodes;
				// Depth first, children in order: hierarchy pre-order.
				std::vector<std::pair<const cgltf_node*, int32_t>> pending;
				for (auto root = roots.rbegin(); root != roots.rend(); ++root)
				{
					pending.emplace_back(*root, -1);
				}
				while (!pending.empty())
				{
					const auto [gltfNode, parent] = pending.back();
					pending.pop_back();
					const auto index = static_cast<uint32_t>(nodes.size());
					nodes.push_back(ConvertNode(*gltfNode));
					ModelNode& node = nodes.back();
					node.Parent = parent;
					const glm::mat4 local = ComposeTransform(node.Translation, node.Rotation, node.Scale);
					if (parent >= 0)
					{
						ModelNode& parentNode = nodes[static_cast<size_t>(parent)];
						parentNode.Children.push_back(index);
						node.ModelTransform = parentNode.ModelTransform * local;
					}
					else
					{
						node.ModelTransform = local;
					}
					for (size_t child = gltfNode->children_count; child-- > 0;)
					{
						pending.emplace_back(gltfNode->children[child], static_cast<int32_t>(index));
					}
				}
				return nodes;
			}

			ModelNode ConvertNode(const cgltf_node& gltfNode)
			{
				ModelNode node;
				const size_t index = static_cast<size_t>(&gltfNode - m_Data.nodes);
				node.Name = gltfNode.name != nullptr && gltfNode.name[0] != '\0' ? std::string(gltfNode.name)
																				 : std::format("Node #{}", index);
				if (gltfNode.has_matrix)
				{
					if (!DecomposeTransform(
							glm::make_mat4(gltfNode.matrix), node.Translation, node.Rotation, node.Scale))
					{
						node.Translation = glm::vec3(gltfNode.matrix[12], gltfNode.matrix[13], gltfNode.matrix[14]);
						Warn(std::format(
							"node {} has a matrix that is not translation, rotation and scale; only its translation is "
							"kept",
							index));
					}
				}
				else
				{
					node.Translation = glm::make_vec3(gltfNode.translation);
					// glTF stores quaternions as x, y, z, w.
					node.Rotation = glm::normalize(glm::quat(
						gltfNode.rotation[3], gltfNode.rotation[0], gltfNode.rotation[1], gltfNode.rotation[2]));
					node.Scale = glm::make_vec3(gltfNode.scale);
				}
				if (gltfNode.mesh != nullptr)
				{
					node.Mesh = m_MeshHandles[cgltf_mesh_index(&m_Data, gltfNode.mesh)];
				}
				if (gltfNode.camera != nullptr)
				{
					node.Camera = ConvertCamera(*gltfNode.camera);
				}
				if (gltfNode.light != nullptr)
				{
					ConvertLight(*gltfNode.light, node);
				}
				return node;
			}

			static CameraComponent ConvertCamera(const cgltf_camera& gltfCamera)
			{
				CameraComponent camera;
				camera.Primary = false;
				if (gltfCamera.type == cgltf_camera_type_orthographic)
				{
					const cgltf_camera_orthographic& orthographic = gltfCamera.data.orthographic;
					camera.Projection = ProjectionType::Orthographic;
					camera.OrthoSize = 2.0f * orthographic.ymag;
					camera.Near = orthographic.znear;
					camera.Far = orthographic.zfar;
				}
				else
				{
					const cgltf_camera_perspective& perspective = gltfCamera.data.perspective;
					camera.Projection = ProjectionType::Perspective;
					camera.VerticalFOV = glm::degrees(perspective.yfov);
					camera.Near = perspective.znear;
					if (perspective.has_zfar)
					{
						camera.Far = perspective.zfar;
					}
				}
				return camera;
			}

			// KHR_lights_punctual: lux for directional lights, candela for point and spot lights (whose lumens are
			// 4 pi times that), ranges in meters (undefined means unlimited), half cone angles in radians.
			static void ConvertLight(const cgltf_light& light, ModelNode& node)
			{
				const glm::vec3 color = glm::make_vec3(light.color);
				switch (light.type)
				{
					case cgltf_light_type_directional:
						node.DirectionalLight = DirectionalLightComponent{ color, light.intensity, true };
						break;
					case cgltf_light_type_point:
						node.PointLight = PointLightComponent{ color, light.intensity * LumensPerCandela, light.range };
						break;
					case cgltf_light_type_spot:
						node.SpotLight = SpotLightComponent{ color, light.intensity * LumensPerCandela, light.range,
							                                 glm::degrees(light.spot_inner_cone_angle),
							                                 glm::degrees(light.spot_outer_cone_angle) };
						break;
					case cgltf_light_type_invalid:
					case cgltf_light_type_max_enum:
						break;
				}
			}

			const AssetLoadContext& m_Context;
			cgltf_data& m_Data;
			std::vector<TextureUse> m_TextureUses;
			std::vector<std::string> m_TextureKeys;
			std::map<size_t, std::optional<Image>> m_Images;
			std::map<std::pair<size_t, bool>, AssetHandle> m_TextureHandles;
			std::vector<AssetHandle> m_MaterialHandles;
			std::vector<AssetHandle> m_MeshHandles;
			std::vector<SubAssetRef> m_Textures;
			std::vector<SubAssetRef> m_Materials;
			std::vector<SubAssetRef> m_Meshes;
			bool m_ExtraInfluences = false;
		};
	}

	Result<Ref<Asset>> GltfImporter::Load(const AssetLoadContext& context) const
	{
		Result<Buffer> bytes = context.Vfs.ReadBinary(context.Record.Path);
		if (!bytes)
		{
			return bytes.GetError();
		}
		FileReader reader{ context.Vfs, {} };
		cgltf_options options{};
		options.file.read = &ReadFile;
		options.file.release = &ReleaseFile;
		options.file.user_data = &reader;

		cgltf_data* parsed = nullptr;
		const cgltf_result parseResult = cgltf_parse(&options, bytes->Data(), bytes->Size(), &parsed);
		DataPointer data(parsed);
		if (parseResult != cgltf_result_success)
		{
			return Error::Format("not a valid glTF 2.0 file: {}", Describe(parseResult));
		}
		for (size_t index = 0; index < data->extensions_required_count; ++index)
		{
			const std::string_view extension = data->extensions_required[index];
			if (std::ranges::find(SupportedRequiredExtensions, extension) == SupportedRequiredExtensions.end())
			{
				return Error::Format(
					"the file requires the glTF extension {}, which Gambit does not support", extension);
			}
		}
		// The buffers are needed for validation (index bounds); external ones are read through the VFS.
		if (const cgltf_result loaded = cgltf_load_buffers(&options, data.get(), context.Record.Path.c_str());
		    loaded != cgltf_result_success)
		{
			return Error::Format(
				"cannot load the buffers: {}{}", Describe(loaded),
				reader.Failure.empty() ? "" : " (" + reader.Failure + ")");
		}
		if (const cgltf_result valid = cgltf_validate(data.get()); valid != cgltf_result_success)
		{
			return Error::Format("the glTF data is inconsistent: {}", Describe(valid));
		}
		return GltfImport(context, *data).Run();
	}
}
