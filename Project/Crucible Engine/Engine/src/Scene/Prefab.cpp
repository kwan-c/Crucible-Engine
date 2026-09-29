/*************************************************************************
  \file       Prefab.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Prefab save and instantiate implementation.
  \details
    Thin by design: all three functions are a version check plus one call
    into EntityIO, which is the same code Scene uses. A prefab really is
    just a one-entity scene without the identity.
  \dependencies
    Prefab.h, EntityIO.h, ECS/World.h, Json/JsonWrapper.h,
    Diagnostics/Logger.h
  \see
    Prefab.h, Scene.cpp, EntityIO.cpp
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Scene/Prefab.h"

#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Scene/EntityIO.h"

namespace fs = std::filesystem;

namespace {

    // Where the single entity's components live inside a prefab file.
    constexpr const char* PREFAB_COMPONENTS = "entity/components";
}

namespace Crucible {

    // =======================================================================
    //  Authoring
    // =======================================================================

    bool Prefab::SaveFrom(World& world,
                          Entity source,
                          const fs::path& file,
                          const std::string& name)
    {
        if (!world.IsAlive(source))
        {
            CRU_ERROR("Prefab: cannot save from a dead entity (index {}, gen {})",
                      source.index, source.generation);
            return false;
        }

        JsonWrapper doc;
        doc.Set("version", kPrefabVersion);
        doc.Set("name", name.empty() ? file.stem().string() : name);

        EntityIO::WriteComponents(world, source, doc, PREFAB_COMPONENTS);

        if (!doc.SaveToFile(file))
        {
            return false;
        }

        CRU_INFO("Prefab: saved '{}' to {}",
                 doc.Get<std::string>("name", std::string{}), file.string());
        return true;
    }

    // =======================================================================
    //  Loading
    // =======================================================================

    bool Prefab::Load(const fs::path& file)
    {
        m_valid = false;

        if (!m_doc.ReadFromFile(file))
        {
            return false;   // already logged
        }

        const int version = m_doc.Get<int>("version", 0);
        if (version > kPrefabVersion)
        {
            CRU_ERROR("Prefab: '{}' is version {}, this build understands {} - "
                      "not loading it",
                      file.string(), version, kPrefabVersion);
            return false;
        }

        m_name  = m_doc.Get<std::string>("name", file.stem().string());
        m_valid = true;
        return true;
    }

    bool Prefab::LoadFromString(const std::string& json)
    {
        m_valid = false;

        if (!m_doc.ReadFromString(json))
        {
            return false;
        }

        const int version = m_doc.Get<int>("version", 0);
        if (version > kPrefabVersion)
        {
            CRU_ERROR("Prefab: text is version {}, this build understands {}",
                      version, kPrefabVersion);
            return false;
        }

        m_name  = m_doc.Get<std::string>("name", std::string{ "Prefab" });
        m_valid = true;
        return true;
    }

    // =======================================================================
    //  Instantiating
    // =======================================================================

    Entity Prefab::Instantiate(World& world) const
    {
        if (!m_valid)
        {
            CRU_ERROR("Prefab: Instantiate called on a prefab that is not loaded");
            return kNullEntity;
        }

        const Entity entity = world.CreateEntity();
        EntityIO::ReadComponents(world, entity, m_doc, PREFAB_COMPONENTS);

        return entity;
    }

}
