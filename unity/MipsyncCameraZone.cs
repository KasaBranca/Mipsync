using UnityEngine;

namespace Mipsync
{
    [DisallowMultipleComponent]
    [AddComponentMenu("Mipsync/Camera Zone")]
    public class MipsyncCameraZone : MonoBehaviour
    {
        [Tooltip("Camera enabled while the player overlaps this trigger volume.")]
        public Camera targetCamera;
    }
}
