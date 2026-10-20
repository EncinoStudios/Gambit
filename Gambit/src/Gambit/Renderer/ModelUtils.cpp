#include "gbpch.h"

#include "Gambit/Renderer/ModelUtils.h"

#include "Gambit/ECS/World.h"

namespace Gambit::ModelUtils
{
	namespace
	{
		std::string DefaultName(const Model& model)
		{
			const std::string& path = model.GetPath();
			const size_t slash = path.find_last_of('/');
			std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
			const size_t dot = name.find_last_of('.');
			if (dot != std::string::npos && dot != 0)
			{
				name.resize(dot);
			}
			return name.empty() ? std::string("Model") : name;
		}
	}

	Result<Entity> Instantiate(
		World& world, const Model& model, Entity parent, const std::optional<TransformComponent>& transform,
		std::string_view name, const InstantiateOptions& options)
	{
		if (options.Ids == UUIDMode::Keep)
		{
			return Error("a model has no entity UUIDs to keep: instantiate it with FromWorld or Random");
		}
		if (world.GetScheduler().IsRunning())
		{
			return Error("models cannot be instantiated while the world's systems run; use the command buffer");
		}
		if (parent.GetWorld() != nullptr && (parent.GetWorld() != &world || !parent.IsValid()))
		{
			return Error("the parent entity does not exist in this world");
		}
		const auto create = [&](std::string_view entityName) {
			return options.Ids == UUIDMode::Random ? world.CreateEntityWithUUID(UUID::Generate(), entityName)
												   : world.CreateEntity(entityName);
		};

		const std::string rootName = name.empty() ? DefaultName(model) : std::string(name);
		Entity root = create(rootName);
		root.AddComponent<TransformComponent>(transform.value_or(TransformComponent{}));
		if (parent)
		{
			if (Result<void> attached = world.SetParent(root, parent); !attached)
			{
				world.DestroyEntity(root);
				return attached.GetError();
			}
		}

		const std::vector<ModelNode>& nodes = model.GetNodes();
		std::vector<Entity> entities(nodes.size());
		for (size_t index = 0; index < nodes.size(); ++index)
		{
			const ModelNode& node = nodes[index];
			Entity entity = create(node.Name);
			entities[index] = entity;
			entity.AddComponent<TransformComponent>(node.Translation, node.Rotation, node.Scale);
			if (node.Mesh.IsValid())
			{
				MeshRendererComponent renderer;
				renderer.Mesh = node.Mesh;
				entity.AddComponent<MeshRendererComponent>(renderer);
			}
			if (node.Camera.has_value())
			{
				entity.AddComponent<CameraComponent>(*node.Camera);
			}
			if (node.DirectionalLight.has_value())
			{
				entity.AddComponent<DirectionalLightComponent>(*node.DirectionalLight);
			}
			if (node.PointLight.has_value())
			{
				entity.AddComponent<PointLightComponent>(*node.PointLight);
			}
			if (node.SpotLight.has_value())
			{
				entity.AddComponent<SpotLightComponent>(*node.SpotLight);
			}
			const Entity nodeParent = node.Parent >= 0 ? entities[static_cast<size_t>(node.Parent)] : root;
			const Result<void> attached = world.SetParent(entity, nodeParent);
			GB_CORE_VERIFY(
				attached.HasValue(), "a new model entity cannot be attached: {}", attached.GetError().Message);
		}
		return root;
	}
}
