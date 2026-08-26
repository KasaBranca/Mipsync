#include "PhysicsWorld.h"
#include "ColliderUtils.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include <cmath>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace MipsyncEngine {

// ─────────────────────────────────────────────────
// Object / broadphase layers
// ─────────────────────────────────────────────────
namespace {

namespace Layers {
    static constexpr JPH::ObjectLayer kNonMoving = 0;
    static constexpr JPH::ObjectLayer kMoving    = 1;
    static constexpr JPH::ObjectLayer kNumLayers = 2;
}

glm::quat SanitizeQuat(glm::quat q) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const float len = glm::length(q);
    if (len < 1e-6f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return q / len;
}

// ─────────────────────────────────────────────────
// glm <-> Jolt converters
// ─────────────────────────────────────────────────
JPH::RVec3 ToJoltPos(const glm::vec3& p)            { return JPH::RVec3(p.x, p.y, p.z); }
JPH::Vec3  ToJoltVec(const glm::vec3& v)            { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat  ToJoltQuat(const glm::quat& q) {
    const glm::quat n = SanitizeQuat(q);
    return JPH::Quat(n.x, n.y, n.z, n.w);
}
glm::vec3  ToGlmPos(const JPH::RVec3& p)            { return { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() }; }
glm::vec3  ToGlmVec(const JPH::Vec3& v)             { return { v.GetX(), v.GetY(), v.GetZ() }; }
glm::vec3  ToGlmEuler(const JPH::Quat& q) {
    const glm::quat gq(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    return glm::degrees(glm::eulerAngles(gq));
}

JPH::EMotionType ToJoltMotion(RigidbodyType t) {
    switch (t) {
        case RigidbodyType::Static:    return JPH::EMotionType::Static;
        case RigidbodyType::Kinematic: return JPH::EMotionType::Kinematic;
        default:                       return JPH::EMotionType::Dynamic;
    }
}

JPH::ObjectLayer ToObjectLayer(RigidbodyType t) {
    return t == RigidbodyType::Static ? Layers::kNonMoving : Layers::kMoving;
}

RigidbodyType ResolveBodyType(const Entity& entity) {
    if (auto* rb = const_cast<Entity&>(entity).GetComponent<RigidbodyComponent>()) {
        if (rb->enabled) return rb->bodyType;
    }
    return RigidbodyType::Static;
}

const Mesh* ResolveMeshForCollider(const Entity& entity, const ColliderComponent& col) {
    if (col.shape != ColliderShape::Mesh) return nullptr;
    if (auto* mr = const_cast<Entity&>(entity).GetComponent<MeshRendererComponent>())
        return mr->mesh.get();
    return nullptr;
}

const char* EntityDebugName(Entity& entity) {
    if (auto* tag = entity.GetComponent<TagComponent>())
        return tag->tag.c_str();
    return "Entity";
}

// Yaw-only orientation (pitch is for the camera, not the body).
glm::quat WorldYawRotation(Scene& scene, Entity& entity, const RigidbodyComponent* rb) {
    if (auto* tr = entity.GetComponent<TransformComponent>()) {
        const glm::quat rot = glm::quat(glm::vec3(
            glm::radians(tr->rotation.x),
            glm::radians(tr->rotation.y),
            glm::radians(tr->rotation.z)));
        return ColliderUtils::PhysicsWorldRotation(rb, SanitizeQuat(rot));
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertCallback(const char* expression, const char* message,
                        const char* file, JPH::uint line) {
    MIPSYNC_ERROR("[Jolt] Assert {}:{} ({}): {}", file, line, expression,
                   message ? message : "");
    return true;
}
#endif

class MipsContactListener;
class MipsCharacterContactListener;

} // namespace

// ─────────────────────────────────────────────────
// Impl
// ─────────────────────────────────────────────────
struct PhysicsWorld::Impl {
    struct StoredContact {
        uint32_t id1 = 0;
        uint32_t id2 = 0;
        bool trigger = false;
    };

    bool initialized = false;
    bool active = false;

    PhysicsContactCallback contactCallback;
    std::unordered_set<uint64_t> activeEntityContactPairs;
    std::unordered_map<JPH::SubShapeIDPair, StoredContact, std::hash<JPH::SubShapeIDPair>>
        subShapeContacts;
    std::unordered_map<const JPH::CharacterVirtual*, uint32_t> characterEntityIds;
    std::unique_ptr<MipsContactListener> contactListener;
    std::unique_ptr<MipsCharacterContactListener> characterContactListener;

    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
    std::unique_ptr<JPH::TempAllocator> tempAllocator;
    std::unique_ptr<JPH::JobSystem> jobSystem;
    std::unique_ptr<JPH::BroadPhaseLayerInterface> broadPhaseInterface;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> objectVsBroadPhaseFilter;
    std::unique_ptr<JPH::ObjectLayerPairFilter> objectLayerPairFilter;

    JPH::BodyInterface* bodyInterface = nullptr;
    std::unordered_map<uint32_t, JPH::BodyID> entityToBody;
    std::unordered_map<uint32_t, JPH::Ref<JPH::CharacterVirtual>> entityToCharacter;

    JPH::Vec3 gravity = JPH::Vec3(0.0f, -9.81f, 0.0f);

    JPH::Ref<JPH::CharacterVirtual> FindCharacter(uint32_t id) {
        auto it = entityToCharacter.find(id);
        return it == entityToCharacter.end() ? nullptr : it->second;
    }
};

namespace {

uint64_t ContactPairKey(uint32_t a, uint32_t b) {
    if (a > b)
        std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}

void EmitContact(PhysicsWorld::Impl* impl, uint32_t selfEntityId, uint32_t otherEntityId,
                 bool isTrigger, bool isEnter) {
    if (!impl || !impl->contactCallback || selfEntityId == 0 || otherEntityId == 0 ||
        selfEntityId == otherEntityId)
        return;
    impl->contactCallback(selfEntityId, otherEntityId, isTrigger, isEnter);
}

class MipsContactListener final : public JPH::ContactListener {
public:
    PhysicsWorld::Impl* impl = nullptr;

    JPH::ValidateResult OnContactValidate(const JPH::Body& /*inBody1*/, const JPH::Body& /*inBody2*/,
                                          JPH::RVec3Arg /*inBaseOffset*/,
                                          const JPH::CollideShapeResult& /*inCollisionResult*/) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& /*ioSettings*/) override {
        if (!impl)
            return;
        const uint32_t id1 = static_cast<uint32_t>(inBody1.GetUserData());
        const uint32_t id2 = static_cast<uint32_t>(inBody2.GetUserData());
        if (!id1 || !id2)
            return;

        const JPH::SubShapeIDPair pair(inBody1.GetID(), inManifold.mSubShapeID1, inBody2.GetID(),
                                       inManifold.mSubShapeID2);
        if (impl->subShapeContacts.contains(pair))
            return;

        const bool trigger = inBody1.IsSensor() || inBody2.IsSensor();
        impl->subShapeContacts.emplace(pair, PhysicsWorld::Impl::StoredContact{ id1, id2, trigger });

        const uint64_t entityKey = ContactPairKey(id1, id2);
        if (impl->activeEntityContactPairs.insert(entityKey).second) {
            EmitContact(impl, id1, id2, trigger, true);
            EmitContact(impl, id2, id1, trigger, true);
        }
    }

    void OnContactPersisted(const JPH::Body& /*inBody1*/, const JPH::Body& /*inBody2*/,
                            const JPH::ContactManifold& /*inManifold*/,
                            JPH::ContactSettings& /*ioSettings*/) override {}

    void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {
        if (!impl)
            return;
        const auto it = impl->subShapeContacts.find(inSubShapePair);
        if (it == impl->subShapeContacts.end())
            return;

        const PhysicsWorld::Impl::StoredContact stored = it->second;
        impl->subShapeContacts.erase(it);

        bool anyLeft = false;
        for (const auto& [pair, contact] : impl->subShapeContacts) {
            if (contact.id1 == stored.id1 && contact.id2 == stored.id2) {
                anyLeft = true;
                break;
            }
        }
        if (anyLeft)
            return;

        const uint64_t entityKey = ContactPairKey(stored.id1, stored.id2);
        if (!impl->activeEntityContactPairs.erase(entityKey))
            return;

        EmitContact(impl, stored.id1, stored.id2, stored.trigger, false);
        EmitContact(impl, stored.id2, stored.id1, stored.trigger, false);
    }
};

class MipsCharacterContactListener final : public JPH::CharacterContactListener {
public:
    PhysicsWorld::Impl* impl = nullptr;

    uint32_t EntityForCharacter(const JPH::CharacterVirtual* character) const {
        if (!impl || !character)
            return 0;
        const auto it = impl->characterEntityIds.find(character);
        return it == impl->characterEntityIds.end() ? 0 : it->second;
    }

    void OnContactAdded(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID,
                        const JPH::SubShapeID& /*inBodySubShapeID*/,
                        JPH::RVec3Arg /*inContactPosition*/, JPH::Vec3Arg /*inContactNormal*/,
                        JPH::CharacterContactSettings& /*ioSettings*/) override {
        if (!impl || !impl->bodyInterface || !inCharacter)
            return;
        const uint32_t selfId = EntityForCharacter(inCharacter);
        if (!selfId)
            return;

        JPH::BodyLockRead lock(impl->physicsSystem->GetBodyLockInterface(), inBodyID);
        if (!lock.Succeeded())
            return;
        const JPH::Body& body = lock.GetBody();
        const uint32_t otherId = static_cast<uint32_t>(body.GetUserData());
        if (!otherId)
            return;

        const uint64_t key = ContactPairKey(selfId, otherId);
        if (!impl->activeEntityContactPairs.insert(key).second)
            return;

        const bool trigger = body.IsSensor();
        EmitContact(impl, selfId, otherId, trigger, true);
        EmitContact(impl, otherId, selfId, trigger, true);
    }
};

} // namespace

// ─────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────
PhysicsWorld::PhysicsWorld() : m_Impl(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { EndPlay(); }

bool PhysicsWorld::IsActive() const { return m_Impl && m_Impl->active; }

void PhysicsWorld::EnsureInitialized() {
    if (m_Impl->initialized) return;

    JPH::RegisterDefaultAllocator();
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertCallback;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    // Single-threaded jobs: thread pool with 0 workers deadlocks in Barrier::Wait.
    m_Impl->jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);

    auto bp = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(Layers::kNumLayers, 2);
    bp->MapObjectToBroadPhaseLayer(Layers::kNonMoving, JPH::BroadPhaseLayer(0));
    bp->MapObjectToBroadPhaseLayer(Layers::kMoving,    JPH::BroadPhaseLayer(1));
    m_Impl->broadPhaseInterface = std::move(bp);

    auto pair = std::make_unique<JPH::ObjectLayerPairFilterTable>(Layers::kNumLayers);
    pair->EnableCollision(Layers::kNonMoving, Layers::kMoving);
    pair->EnableCollision(Layers::kMoving,    Layers::kNonMoving);
    pair->EnableCollision(Layers::kMoving,    Layers::kMoving);
    m_Impl->objectLayerPairFilter = std::move(pair);

    m_Impl->objectVsBroadPhaseFilter =
        std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
            *m_Impl->broadPhaseInterface, 2,
            *m_Impl->objectLayerPairFilter, Layers::kNumLayers);

    m_Impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_Impl->physicsSystem->Init(4096, 0, 1024, 1024,
                                *m_Impl->broadPhaseInterface,
                                *m_Impl->objectVsBroadPhaseFilter,
                                *m_Impl->objectLayerPairFilter);
    m_Impl->physicsSystem->SetGravity(m_Impl->gravity);

    m_Impl->bodyInterface = &m_Impl->physicsSystem->GetBodyInterface();

    m_Impl->contactListener = std::make_unique<MipsContactListener>();
    m_Impl->contactListener->impl = m_Impl.get();
    m_Impl->physicsSystem->SetContactListener(m_Impl->contactListener.get());

    m_Impl->characterContactListener = std::make_unique<MipsCharacterContactListener>();
    m_Impl->characterContactListener->impl = m_Impl.get();

    m_Impl->initialized = true;
    MIPSYNC_INFO("Jolt Physics initialized");
}

void PhysicsWorld::SetContactCallback(PhysicsContactCallback callback) {
    m_Impl->contactCallback = std::move(callback);
}

void PhysicsWorld::DestroyAllBodies() {
    // Destroy CharacterVirtuals first; their destructors remove the inner bodies they own.
    m_Impl->entityToCharacter.clear();
    m_Impl->characterEntityIds.clear();
    m_Impl->activeEntityContactPairs.clear();
    m_Impl->subShapeContacts.clear();

    if (!m_Impl->bodyInterface) return;
    for (auto& [id, body] : m_Impl->entityToBody) {
        if (m_Impl->bodyInterface->IsAdded(body))
            m_Impl->bodyInterface->RemoveBody(body);
        m_Impl->bodyInterface->DestroyBody(body);
        (void)id;
    }
    m_Impl->entityToBody.clear();
}

void PhysicsWorld::BeginPlay(Scene& scene) {
    EnsureInitialized();
    EndPlay();
    m_Impl->activeEntityContactPairs.clear();
    m_Impl->subShapeContacts.clear();
    m_Impl->active = true;
    RebuildBodies(scene);
    MIPSYNC_INFO("Physics play: {} bodies, {} characters",
                  m_Impl->entityToBody.size(), m_Impl->entityToCharacter.size());
}

void PhysicsWorld::EndPlay() {
    if (!m_Impl->active && m_Impl->entityToBody.empty() && m_Impl->entityToCharacter.empty())
        return;
    DestroyAllBodies();
    m_Impl->active = false;
}

void PhysicsWorld::RefreshBodies(Scene& scene) {
    if (!m_Impl->active) return;
    RebuildBodies(scene);
}

// ─────────────────────────────────────────────────
// Body / character creation
// ─────────────────────────────────────────────────
namespace {

JPH::Ref<JPH::CharacterVirtual> CreateCharacterController(
    JPH::PhysicsSystem& system, const ColliderComponent& col, const glm::vec3& scale,
    const glm::vec3& position, const glm::quat& rotation,
    JPH::CharacterContactListener* contactListener) {
    if (col.shape != ColliderShape::Capsule)
        return nullptr;

    const float r = std::max(0.05f, col.radius * std::max(scale.x, scale.z));
    const float halfH = std::max(0.01f, (col.capsuleHeight * 0.5f) * scale.y);

    JPH::CapsuleShapeSettings shapeSettings(halfH, r);
    JPH::RefConst<JPH::Shape> shape = shapeSettings.Create().Get();
    if (!shape) return nullptr;

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = shape;
    settings->mUp = JPH::Vec3(0.0f, 1.0f, 0.0f);
    // Capsule offset from the entity pivot (e.g. eye height) to the capsule center.
    settings->mShapeOffset = JPH::Vec3(col.center.x * scale.x,
                                       col.center.y * scale.y,
                                       col.center.z * scale.z);
    settings->mMass = 70.0f;
    settings->mMaxStrength = 100.0f;
    settings->mPenetrationRecoverySpeed = 1.0f;
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    settings->mInnerBodyShape = nullptr;

    JPH::Ref<JPH::CharacterVirtual> character =
        new JPH::CharacterVirtual(settings, ToJoltPos(position), ToJoltQuat(rotation), &system);
    if (character && contactListener)
        character->SetListener(contactListener);
    return character;
}

void ConfigureBodySettings(JPH::BodyCreationSettings& settings, const RigidbodyComponent* rb,
                           RigidbodyType effectiveBodyType) {
    if (!rb || !rb->enabled) return;
    settings.mFriction = 0.5f;
    settings.mRestitution = std::clamp(rb->bounciness, 0.0f, 1.0f);
    settings.mLinearDamping = std::max(0.0f, rb->linearDrag);
    settings.mGravityFactor = rb->useGravity ? 1.0f : 0.0f;
    if (effectiveBodyType == RigidbodyType::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(0.001f, rb->mass);
    }
    if (rb->freezeRotation) {
        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                                JPH::EAllowedDOFs::TranslationY |
                                JPH::EAllowedDOFs::TranslationZ;
    }
}

} // namespace

void PhysicsWorld::RebuildBodies(Scene& scene) {
    DestroyAllBodies();
    if (!m_Impl->bodyInterface) return;

    std::vector<Entity*> characterEntities;
    std::vector<Entity*> rigidEntities;

    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        if (!entity->IsActive()) continue;
        auto* collider = entity->GetComponent<ColliderComponent>();
        if (!collider || !collider->enabled) continue;
        if (!entity->GetComponent<TransformComponent>()) continue;

        auto* rb = entity->GetComponent<RigidbodyComponent>();
        if (rb && ColliderUtils::IsCharacterController(rb))
            characterEntities.push_back(entity);
        else
            rigidEntities.push_back(entity);
    }

    auto addRigidBody = [&](Entity* entity) {
        auto* collider = entity->GetComponent<ColliderComponent>();
        auto* rb = entity->GetComponent<RigidbodyComponent>();
        const RigidbodyType requestedBodyType = ResolveBodyType(*entity);
        RigidbodyType bodyType = requestedBodyType;
        if (collider->shape == ColliderShape::Mesh && !collider->convex &&
            bodyType != RigidbodyType::Static) {
            MIPSYNC_WARN("Physics: non-convex mesh collider on '{}' requires static physics; "
                         "ignoring its moving Rigidbody mode", EntityDebugName(*entity));
            bodyType = RigidbodyType::Static;
        }
        const ColliderUtils::ColliderWorldPose pose =
            ColliderUtils::ComputePhysicsWorldPose(scene, *entity, *collider, rb);
        const Mesh* mesh = ResolveMeshForCollider(*entity, *collider);
        JPH::RefConst<JPH::Shape> shape = ColliderUtils::CreateShape(*collider, mesh, pose.lossyScale);
        if (!shape) {
            MIPSYNC_WARN("Physics: failed to create shape for '{}'", EntityDebugName(*entity));
            return;
        }

        JPH::BodyCreationSettings settings(shape, ToJoltPos(pose.center), ToJoltQuat(pose.rotation),
                                           ToJoltMotion(bodyType), ToObjectLayer(bodyType));
        ConfigureBodySettings(settings, rb, bodyType);
        if (collider->isTrigger) settings.mIsSensor = true;
        settings.mUserData = static_cast<JPH::uint64>(entity->GetID());

        // Static bodies: CreateAndAddBody tends to be safest across setups.
        if (bodyType == RigidbodyType::Static) {
            const JPH::BodyID bodyId = m_Impl->bodyInterface->CreateAndAddBody(
                settings, JPH::EActivation::DontActivate);
            if (bodyId.IsInvalid()) {
                MIPSYNC_WARN("Physics: CreateAndAddBody failed for '{}'", EntityDebugName(*entity));
                return;
            }
            m_Impl->entityToBody[entity->GetID()] = bodyId;
            return;
        }

        // Dynamic/kinematic bodies: CreateBody + AddBody (gives more checkpoints for logs).
        JPH::Body* body = m_Impl->bodyInterface->CreateBody(settings);
        if (!body) {
            MIPSYNC_WARN("Physics: CreateBody failed for '{}'", EntityDebugName(*entity));
            return;
        }
        const JPH::BodyID bodyId = body->GetID();
        if (bodyId.IsInvalid()) {
            MIPSYNC_WARN("Physics: invalid BodyID for '{}'", EntityDebugName(*entity));
            return;
        }
        m_Impl->bodyInterface->AddBody(bodyId, JPH::EActivation::Activate);
        m_Impl->entityToBody[entity->GetID()] = bodyId;
    };

    auto addCharacter = [&](Entity* entity) {
        auto* collider = entity->GetComponent<ColliderComponent>();
        auto* rb = entity->GetComponent<RigidbodyComponent>();
        const glm::vec3 pivot = scene.GetWorldPosition(*entity);
        const glm::quat yawRot = WorldYawRotation(scene, *entity, rb);
        const glm::vec3 scale = ColliderUtils::GetLossyScale(scene.GetWorldMatrix(*entity));

        auto character = CreateCharacterController(
            *m_Impl->physicsSystem, *collider, scale, pivot, yawRot,
            m_Impl->characterContactListener.get());
        if (!character) {
            MIPSYNC_WARN("Physics: CharacterVirtual failed for '{}'", EntityDebugName(*entity));
            return;
        }
        m_Impl->entityToCharacter[entity->GetID()] = character;
        m_Impl->characterEntityIds[character.GetPtr()] = entity->GetID();
    };

    for (Entity* entity : rigidEntities)
        addRigidBody(entity);
    for (Entity* entity : characterEntities)
        addCharacter(entity);
}

// ─────────────────────────────────────────────────
// Per-entity sync (gizmo edits during play)
// ─────────────────────────────────────────────────
void PhysicsWorld::SyncEntityFromScene(Scene& scene, Entity& entity) {
    if (!m_Impl->active) return;

    if (auto character = m_Impl->FindCharacter(entity.GetID())) {
        auto* rb = entity.GetComponent<RigidbodyComponent>();
        const glm::vec3 pivot = scene.GetWorldPosition(entity);
        const glm::quat yawRot = WorldYawRotation(scene, entity, rb);
        character->SetPosition(ToJoltPos(pivot));
        character->SetRotation(ToJoltQuat(yawRot));
        character->SetLinearVelocity(JPH::Vec3::sZero());
        return;
    }

    auto it = m_Impl->entityToBody.find(entity.GetID());
    if (it == m_Impl->entityToBody.end()) return;
    auto* collider = entity.GetComponent<ColliderComponent>();
    if (!collider || !collider->enabled) return;

    auto* rb = entity.GetComponent<RigidbodyComponent>();
    const ColliderUtils::ColliderWorldPose pose =
        ColliderUtils::ComputePhysicsWorldPose(scene, entity, *collider, rb);

    const JPH::BodyID bodyId = it->second;
    const JPH::EMotionType motion = m_Impl->bodyInterface->GetMotionType(bodyId);
    m_Impl->bodyInterface->SetPositionAndRotation(
        bodyId, ToJoltPos(pose.center), ToJoltQuat(pose.rotation), JPH::EActivation::Activate);
    if (motion == JPH::EMotionType::Dynamic) {
        m_Impl->bodyInterface->SetLinearVelocity(bodyId, JPH::Vec3::sZero());
        m_Impl->bodyInterface->SetAngularVelocity(bodyId, JPH::Vec3::sZero());
    }
}

// ─────────────────────────────────────────────────
// Per-frame sync helpers
// ─────────────────────────────────────────────────
void PhysicsWorld::SyncKinematicAndStaticFromScene(Scene& scene) {
    if (!m_Impl->bodyInterface) return;

    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        auto it = m_Impl->entityToBody.find(entity->GetID());
        if (it == m_Impl->entityToBody.end()) continue;

        const JPH::EMotionType motion = m_Impl->bodyInterface->GetMotionType(it->second);
        if (motion != JPH::EMotionType::Kinematic && motion != JPH::EMotionType::Static)
            continue;

        auto* collider = entity->GetComponent<ColliderComponent>();
        if (!collider || !collider->enabled) continue;

        auto* rb = entity->GetComponent<RigidbodyComponent>();
        const ColliderUtils::ColliderWorldPose pose =
            ColliderUtils::ComputePhysicsWorldPose(scene, *entity, *collider, rb);

        m_Impl->bodyInterface->SetPositionAndRotation(
            it->second, ToJoltPos(pose.center), ToJoltQuat(pose.rotation),
            motion == JPH::EMotionType::Kinematic ? JPH::EActivation::Activate
                                                  : JPH::EActivation::DontActivate);
    }
}

void PhysicsWorld::SyncDynamicToScene(Scene& scene) {
    if (!m_Impl->bodyInterface) return;

    for (const auto& entityPtr : scene.GetEntities()) {
        Entity* entity = entityPtr.get();
        auto it = m_Impl->entityToBody.find(entity->GetID());
        if (it == m_Impl->entityToBody.end()) continue;

        if (m_Impl->bodyInterface->GetMotionType(it->second) != JPH::EMotionType::Dynamic)
            continue;

        auto* transform = entity->GetComponent<TransformComponent>();
        auto* collider = entity->GetComponent<ColliderComponent>();
        if (!transform || !collider || !collider->enabled) continue;

        glm::vec3 worldPos = ToGlmPos(m_Impl->bodyInterface->GetPosition(it->second));
        const glm::vec3 worldEuler = ToGlmEuler(m_Impl->bodyInterface->GetRotation(it->second));

        if (glm::length(collider->center) > 0.0001f) {
            const glm::quat worldRot = glm::quat(glm::radians(worldEuler));
            const glm::vec3 lossyScale = ColliderUtils::GetLossyScale(scene.GetWorldMatrix(*entity));
            worldPos -= worldRot * (collider->center * lossyScale);
        }

        if (entity->GetParentID() == 0) {
            transform->position = worldPos;
            transform->rotation = worldEuler;
        } else {
            scene.SetWorldPosition(*entity, worldPos);
            transform->rotation = worldEuler;
        }
    }
}

// ─────────────────────────────────────────────────
// CharacterVirtual integration
// ─────────────────────────────────────────────────
void PhysicsWorld::UpdateCharacters(Scene& scene, float deltaTime) {
    if (m_Impl->entityToCharacter.empty()) return;

    const auto bpFilter = m_Impl->physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::kMoving);
    const auto layerFilter = m_Impl->physicsSystem->GetDefaultLayerFilter(Layers::kMoving);

    for (auto& [id, character] : m_Impl->entityToCharacter) {
        // SetCharacterVelocity (called from Mips Update) wrote the velocity directly.
        Entity* entity = scene.FindEntity(id);
        const auto* rb = entity ? entity->GetComponent<RigidbodyComponent>() : nullptr;
        const JPH::Vec3 gravity = (!rb || rb->useGravity)
            ? m_Impl->gravity
            : JPH::Vec3::sZero();
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        // Do not magnetically snap controllers down to nearby floors. Gravity
        // already keeps ordinary characters grounded, while fixed-camera
        // controllers retain their authored vertical plane.
        settings.mStickToFloorStepDown = JPH::Vec3::sZero();
        character->ExtendedUpdate(deltaTime, gravity, settings,
                                  bpFilter, layerFilter,
                                  JPH::BodyFilter(), JPH::ShapeFilter(),
                                  *m_Impl->tempAllocator);
        (void)id;
    }
}

void PhysicsWorld::SyncCharactersToScene(Scene& scene) {
    for (const auto& [id, character] : m_Impl->entityToCharacter) {
        Entity* entity = scene.FindEntity(id);
        if (!entity) continue;
        scene.SetWorldPosition(*entity, ToGlmPos(character->GetPosition()));
    }
}

bool PhysicsWorld::SetCharacterVelocity(Entity& entity, const glm::vec3& velocity) {
    if (auto character = m_Impl->FindCharacter(entity.GetID())) {
        character->SetLinearVelocity(ToJoltVec(velocity));
        return true;
    }
    return false;
}

bool PhysicsWorld::IsCharacterGrounded(Entity& entity) const {
    if (auto character = m_Impl->FindCharacter(entity.GetID()))
        return character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    return false;
}

// ─────────────────────────────────────────────────
// Main step
// ─────────────────────────────────────────────────
void PhysicsWorld::Simulate(Scene& scene, float deltaTime) {
    if (!m_Impl->active || !m_Impl->physicsSystem) return;
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) return;

    deltaTime = std::min(deltaTime, 1.0f / 20.0f);

    SyncKinematicAndStaticFromScene(scene);

    // 1) Move CharacterVirtuals (slides along surfaces, sticks to ground, walks stairs).
    UpdateCharacters(scene, deltaTime);

    // 2) Step the regular rigid body simulation (substepped at ~60Hz).
    const int collisionSteps = std::clamp(static_cast<int>(std::ceil(deltaTime / (1.0f / 60.0f))), 1, 4);
    const float stepDt = deltaTime / static_cast<float>(collisionSteps);
    for (int i = 0; i < collisionSteps; ++i)
        m_Impl->physicsSystem->Update(stepDt, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());

    // 3) Write physics → scene transforms.
    SyncDynamicToScene(scene);
    SyncCharactersToScene(scene);
}

} // namespace MipsyncEngine
