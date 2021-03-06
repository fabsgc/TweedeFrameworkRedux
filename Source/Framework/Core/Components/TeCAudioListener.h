#pragma once

#include "TeCorePrerequisites.h"
#include "Audio/TeAudioListener.h"
#include "Scene/TeComponent.h"

namespace te
{
    /**
     * @copydoc	AudioListener
     *
     * @note Wraps AudioListener as a Component.
     */
    class TE_CORE_EXPORT CAudioListener : public Component
    {
    public:
        CAudioListener(const HSceneObject& parent);

        virtual ~CAudioListener() = default;

        static UINT32 GetComponentType() { return TID_CAudioListener; }

        /** @copydoc Component::Initialize */
        void Initialize() override;

        /** @copydoc Component::Clone */
        void Clone(const HComponent& c) override;

        /** @copydoc Component::Clone */
        void Clone(const HAudioListener& c);

        /* @copydoc Component::MarkDirty */
        virtual void MarkDirty() { _internal->MarkCoreDirty(); }

        /** @copydoc Component::Update */
        void Update() override;

        /** Returns the AudioListener implementation wrapped by this component. */
        AudioListener* _getInternal() const { return _internal.get(); }

        /** @copydoc AudioListener::GetTransform */
        const Transform& GetTransform() { return _internal->GetTransform(); }

        /** @copydoc SceneActor::GetActive */
        bool GetActive() const { return _internal != nullptr; }

    protected:
        friend class SceneObject;

        /** @copydoc Component::_instantiate */
        void _instantiate() override {}

        /** @copydoc Component::OnInitialized */
        void OnInitialized() override;

        /** @copydoc Component::OnEnabled */
        void OnEnabled() override;

        /** @copydoc Component::OnEnabled */
        void OnDisabled() override;

        /** @copydoc Component::OnTransformChanged */
        void OnTransformChanged(TransformChangedFlags flags) override;

        /** @copydoc Component::OnDestroyed */
        void OnDestroyed() override;

    protected:
        using Component::DestroyInternal;

        /**
        * Creates the internal representation of the Animation and restores the values saved by the Component.
        * Node : clips must be added manually because this method can't do it properly
        */
        void RestoreInternal();

        /** Destroys the internal Animation representation. */
        void DestroyInternal();

        /**
         * Updates the transform of the internal AudioListener representation from the transform of the component's scene
         * object.
         */
        void UpdateTransform();

    protected:
        CAudioListener();

    protected:
        SPtr<AudioListener> _internal;
        Vector3 _lastPosition = Vector3::ZERO;
        Vector3 _velocity = Vector3::ZERO;
    };
}
