/*
 * Marks a Unity mesh as static occlusion geometry for a pre-rendered scene.
 * The mesh remains part of the captured background, but Mipsync exports its
 * geometry so the PS1 runtime can restore background pixels at the right depth.
 */

using UnityEngine;

namespace Mipsync
{
    [DisallowMultipleComponent]
    [AddComponentMenu("Mipsync/Prerender Occluder")]
    public sealed class MipsyncPrerenderOccluder : MonoBehaviour
    {
        [Tooltip("Export this object's MeshFilter or MeshCollider geometry as pre-rendered occlusion.")]
        public bool exportOccluder = true;
    }
}
