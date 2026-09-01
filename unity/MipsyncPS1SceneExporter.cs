/*
 * MipsyncPS1SceneExporter.cs
 *
 * Attach this component to a Camera in Unity.
 * This class contains only the MonoBehaviour logic and resides outside the Editor folder
 * so that it can be attached to scene GameObjects.
 */

using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEngine;

namespace Mipsync
{
    [RequireComponent(typeof(Camera))]
    [AddComponentMenu("Mipsync/PS1 Scene Exporter")]
    public class MipsyncPS1SceneExporter : MonoBehaviour
    {
        [Header("Export Settings")]
        [Tooltip("Output directory for the .nscene and background PNG (relative to project root or absolute).")]
        public string exportPath = "MipsyncExport";

        [Tooltip("Scene name used for the output .nscene filename.")]
        public string sceneName = "exported_scene";

        [Tooltip("Export every Mipsync PS1 Scene Exporter camera in the Unity scene into one .nscene.")]
        public bool exportAllSceneExporters = true;

        [Header("Background")]
        [Tooltip("Capture a pre-rendered background from this camera.")]
        public bool captureBackground = true;

        [Tooltip("Background capture width (PS1 native = 320).")]
        public int backgroundWidth = 320;

        [Tooltip("Background capture height (PS1 native = 240).")]
        public int backgroundHeight = 240;

        [Tooltip("Distance from the camera for the editor-only background preview plane.")]
        public float backgroundPreviewDistance = 12.0f;

        [Header("Colliders")]
        [Tooltip("Export supported colliders on GameObjects tagged ExpObject. Camera switching triggers are always exported.")]
        public bool includeColliders = true;

        [Tooltip("Layer mask for ExpObject collider export. Camera switching triggers ignore this mask.")]
        public LayerMask colliderLayerMask = ~0;

        [Tooltip("Legacy fallback tag used to mark colliders as camera shot triggers. Prefer Mipsync Camera Zone.")]
        public string cameraTriggerTag = "MipsyncCameraTrigger";

        [Header("Camera")]
        [Tooltip("Mark this camera as the primary PS1 camera.")]
        public bool isPrimaryCamera = true;

        [Tooltip("Deprecated. Use collider tags for camera shot triggers instead.")]
        public Collider activationTrigger = null;

        [Tooltip("Higher priority shots win when multiple trigger volumes overlap.")]
        public int shotPriority = 0;

        private sealed class CameraExportData
        {
            public MipsyncPS1SceneExporter exporter;
            public Camera camera;
            public string backgroundPath;
            public int entityId;
            public int previewEntityId;
            public bool primary;
        }

        /// <summary>
        /// Performs the full export: camera + background + colliders -> .nscene JSON + PNG.
        /// </summary>
        public void ExportScene()
        {
            var cam = GetComponent<Camera>();
            if (cam == null)
            {
                Debug.LogError("[MipsyncPS1SceneExporter] No Camera component found.");
                return;
            }

            // Resolve output directory.
            string outputDir = exportPath;
            if (!Path.IsPathRooted(outputDir))
                outputDir = Path.Combine(Application.dataPath, "..", outputDir);
            outputDir = Path.GetFullPath(outputDir);
            Directory.CreateDirectory(outputDir);

            var exporters = GetExportersForCurrentExport();
            var cameraExports = new List<CameraExportData>();
            bool exportingMultipleCameras = exporters.Count > 1;
            foreach (var exporter in exporters)
            {
                var exportCamera = exporter.GetComponent<Camera>();
                if (exportCamera == null)
                    continue;

                string bgRelativePath = "";
                if (exporter.captureBackground)
                {
                    string bgName = exportingMultipleCameras
                        ? sceneName + "_" + MakeSafeFileName(exportCamera.gameObject.name)
                        : sceneName;
                    bgRelativePath = exporter.CaptureBackground(exportCamera, outputDir, bgName);
                }

                cameraExports.Add(new CameraExportData {
                    exporter = exporter,
                    camera = exportCamera,
                    backgroundPath = bgRelativePath
                });
            }

            if (cameraExports.Count == 0)
            {
                Debug.LogError("[MipsyncPS1SceneExporter] No cameras found to export.");
                return;
            }

            // --- Build nscene JSON ---
            var sceneJson = BuildSceneJson(cameraExports);

            string nscenePath = Path.Combine(outputDir, sceneName + ".nscene");
            File.WriteAllText(nscenePath, sceneJson);

            Debug.Log($"[MipsyncPS1SceneExporter] Exported to: {nscenePath}");
        }

        private List<MipsyncPS1SceneExporter> GetExportersForCurrentExport()
        {
            var exporters = new List<MipsyncPS1SceneExporter>();
            if (!exportAllSceneExporters)
            {
                exporters.Add(this);
                return exporters;
            }

            exporters.AddRange(FindObjectsOfType<MipsyncPS1SceneExporter>());
            exporters.Sort((a, b) => {
                if (a == this) return -1;
                if (b == this) return 1;
                int sceneOrder = string.CompareOrdinal(GetHierarchyPath(a.transform),
                                                       GetHierarchyPath(b.transform));
                return sceneOrder != 0
                    ? sceneOrder
                    : a.GetInstanceID().CompareTo(b.GetInstanceID());
            });
            return exporters;
        }

        private string CaptureBackground(Camera cam, string outputDir, string filenameBase)
        {
            string bgDir = Path.Combine(outputDir, "backgrounds");
            Directory.CreateDirectory(bgDir);

            string bgFilename = MakeSafeFileName(filenameBase) + "_bg.png";
            string bgFullPath = Path.Combine(bgDir, bgFilename);

            var rt = new RenderTexture(backgroundWidth, backgroundHeight, 24, RenderTextureFormat.ARGB32);
            rt.antiAliasing = 1;
            Texture2D tex2d = null;
            bool prevCamEnabled = cam.enabled;

            Camera[] sceneCameras = FindObjectsOfType<Camera>();
            var previousEnabled = new Dictionary<Camera, bool>();
            foreach (var sceneCamera in sceneCameras)
            {
                if (sceneCamera == null)
                    continue;
                previousEnabled[sceneCamera] = sceneCamera.enabled;
                sceneCamera.enabled = sceneCamera == cam;
            }

            var prevRT = cam.targetTexture;
            var prevActive = RenderTexture.active;
            try
            {
                cam.enabled = true;
                cam.targetTexture = rt;
                cam.Render();

                RenderTexture.active = rt;
                tex2d = new Texture2D(backgroundWidth, backgroundHeight, TextureFormat.RGB24, false);
                tex2d.ReadPixels(new Rect(0, 0, backgroundWidth, backgroundHeight), 0, 0);
                tex2d.Apply();

                byte[] pngBytes = tex2d.EncodeToPNG();
                File.WriteAllBytes(bgFullPath, pngBytes);
            }
            finally
            {
                cam.targetTexture = prevRT;
                cam.enabled = prevCamEnabled;
                RenderTexture.active = prevActive;
                foreach (var pair in previousEnabled)
                {
                    if (pair.Key != null)
                        pair.Key.enabled = pair.Value;
                }

                if (Application.isPlaying)
                {
                    if (tex2d != null) Destroy(tex2d);
                    Destroy(rt);
                }
                else
                {
                    if (tex2d != null) DestroyImmediate(tex2d);
                    DestroyImmediate(rt);
                }
            }

            Debug.Log($"[MipsyncPS1SceneExporter] Background captured: {bgFullPath} ({backgroundWidth}x{backgroundHeight})");
            return "backgrounds/" + bgFilename;
        }

        private static string GetHierarchyPath(Transform transform)
        {
            if (transform == null)
                return "";
            var names = new List<string>();
            Transform current = transform;
            while (current != null)
            {
                names.Add(current.name);
                current = current.parent;
            }
            names.Reverse();
            return string.Join("/", names);
        }

        private string BuildSceneJson(List<CameraExportData> cameraExports)
        {
            var entities = new List<string>();
            int nextId = 1;

            foreach (var cameraExport in cameraExports)
            {
                cameraExport.entityId = nextId++;
                if (!string.IsNullOrEmpty(cameraExport.backgroundPath))
                    cameraExport.previewEntityId = nextId++;
            }

            var cameraEntityIdByCamera = new Dictionary<Camera, int>();
            var entityIdByTransform = new Dictionary<Transform, int>();
            foreach (var cameraExport in cameraExports)
            {
                cameraEntityIdByCamera[cameraExport.camera] = cameraExport.entityId;
                entityIdByTransform[cameraExport.camera.transform] = cameraExport.entityId;
            }

            CameraExportData primaryCamera = null;
            foreach (var cameraExport in cameraExports)
            {
                if (primaryCamera != null)
                    break;
                if (!cameraExport.exporter.isPrimaryCamera)
                    continue;
                primaryCamera = cameraExport;
            }
            if (primaryCamera == null && Camera.main != null)
            {
                foreach (var cameraExport in cameraExports)
                {
                    if (cameraExport.camera == Camera.main)
                    {
                        primaryCamera = cameraExport;
                        break;
                    }
                }
            }
            if (primaryCamera == null)
            {
                foreach (var cameraExport in cameraExports)
                {
                    if (cameraExport.exporter == this)
                    {
                        primaryCamera = cameraExport;
                        break;
                    }
                }
            }
            if (primaryCamera == null && cameraExports.Count > 0)
                primaryCamera = cameraExports[0];
            if (primaryCamera != null)
                primaryCamera.primary = true;

            foreach (var cameraExport in cameraExports)
            {
                Debug.Log("[MipsyncPS1SceneExporter] Camera export: " +
                          cameraExport.camera.gameObject.name +
                          $" primary={cameraExport.primary} unityPrimary={cameraExport.exporter.isPrimaryCamera} " +
                          $"background={cameraExport.backgroundPath}");
            }

            var exporterObjects = new HashSet<GameObject>();
            foreach (var cameraExport in cameraExports)
                exporterObjects.Add(cameraExport.exporter.gameObject);

            var occluderEntities = new List<MipsyncPrerenderOccluder>();
            foreach (var occluder in Resources.FindObjectsOfTypeAll<MipsyncPrerenderOccluder>())
            {
                if (occluder == null || !occluder.exportOccluder)
                    continue;
                if (!occluder.gameObject.scene.IsValid() ||
                    !occluder.gameObject.scene.isLoaded)
                    continue;
                if (!occluder.isActiveAndEnabled || !occluder.gameObject.activeInHierarchy)
                    continue;
                var occluderMesh = GetPrerenderOccluderMesh(occluder);
                if (occluderMesh == null)
                {
                    Debug.LogWarning("[MipsyncPS1SceneExporter] Prerender Occluder has no " +
                                     "MeshFilter/MeshCollider mesh: " +
                                     GetHierarchyPath(occluder.transform));
                    continue;
                }
                occluderEntities.Add(occluder);
                Debug.Log("[MipsyncPS1SceneExporter] Prerender Occluder export: " +
                          GetHierarchyPath(occluder.transform) +
                          $" active={occluder.gameObject.activeInHierarchy} " +
                          $"componentEnabled={occluder.enabled} " +
                          $"vertices={occluderMesh.vertexCount}");
            }
            if (occluderEntities.Count == 0)
            {
                Debug.LogWarning(
                    "[MipsyncPS1SceneExporter] No Mipsync Prerender Occluder found. " +
                    "Pre-rendered foreground objects cannot hide characters.");
            }
            else
            {
                Debug.Log("[MipsyncPS1SceneExporter] Prerender occluders: " +
                          occluderEntities.Count);
            }
            occluderEntities.Sort((a, b) =>
                string.CompareOrdinal(GetHierarchyPath(a.transform), GetHierarchyPath(b.transform)));

            var occluderEntityIdByOccluder =
                new Dictionary<MipsyncPrerenderOccluder, int>();
            foreach (var occluder in occluderEntities)
            {
                int entityId = nextId++;
                occluderEntityIdByOccluder[occluder] = entityId;
                if (!entityIdByTransform.ContainsKey(occluder.transform))
                    entityIdByTransform[occluder.transform] = entityId;
            }

            var colliderEntities = new List<Collider>();
            var colliders = FindObjectsOfType<Collider>();
            foreach (var col in colliders)
            {
                if (col == null)
                    continue;
                if (!IsSupportedCollider(col))
                    continue;

                bool cameraTrigger = IsCameraTriggerCollider(col) ||
                                     col.GetComponent<MipsyncCameraZone>() != null;
                if (exporterObjects.Contains(col.gameObject) && !cameraTrigger)
                    continue; // Ordinary colliders on exporter cameras are not scene objects.
                if (!cameraTrigger)
                {
                    if (!includeColliders || !IsExpObject(col.gameObject))
                        continue;
                    if (((1 << col.gameObject.layer) & colliderLayerMask.value) == 0)
                        continue;
                }

                colliderEntities.Add(col);
            }

            var colliderEntityIdByCollider = new Dictionary<Collider, int>();
            foreach (var col in colliderEntities)
            {
                int entityId = nextId++;
                colliderEntityIdByCollider[col] = entityId;
                if (!entityIdByTransform.ContainsKey(col.transform))
                    entityIdByTransform[col.transform] = entityId;
            }

            Light exportedLight = null;
            int exportedLightEntityId = 0;
            var lights = FindObjectsOfType<Light>();
            foreach (var light in lights)
            {
                if (light.type == LightType.Directional && IsExpObject(light.gameObject))
                {
                    exportedLight = light;
                    exportedLightEntityId = nextId++;
                    if (!entityIdByTransform.ContainsKey(light.transform))
                        entityIdByTransform[light.transform] = exportedLightEntityId;
                    break;
                }
            }

            // --- Camera entities + editor-only pre-render preview planes ---
            foreach (var cameraExport in cameraExports)
            {
                entities.Add(BuildCameraEntityJson(cameraExport, entityIdByTransform));
                if (!string.IsNullOrEmpty(cameraExport.backgroundPath))
                    entities.Add(BuildBackgroundPreviewEntityJson(cameraExport));
            }

            // --- Static geometry used to restore pre-rendered background pixels by depth ---
            foreach (var occluder in occluderEntities)
            {
                entities.Add(BuildPrerenderOccluderEntityJson(
                    occluder,
                    occluderEntityIdByOccluder[occluder],
                    entityIdByTransform));
            }

            // --- Collider entities ---
            foreach (var col in colliderEntities)
            {
                entities.Add(BuildColliderEntityJson(
                    col,
                    colliderEntityIdByCollider[col],
                    cameraEntityIdByCamera,
                    entityIdByTransform));
            }

            // --- Light (first directional) ---
            if (exportedLight != null)
            {
                entities.Add(BuildLightEntityJson(
                    exportedLight,
                    exportedLightEntityId,
                    entityIdByTransform));
            }

            string entitiesArray = "[\n" + string.Join(",\n", entities) + "\n  ]";
            return "{\n  \"entities\": " + entitiesArray + "\n}";
        }

        private string BuildCameraEntityJson(CameraExportData cameraExport,
                                             Dictionary<Transform, int> entityIdByTransform)
        {
            Camera cam = cameraExport.camera;
            var t = cam.transform;
            int parentId = GetExportedParentId(t, entityIdByTransform);
            var pos = ToMipsyncPosition(parentId != 0 ? t.localPosition : t.position);
            var rot = ToMipsyncEuler(parentId != 0 ? t.localRotation : t.rotation);
            var scl = parentId != 0 ? t.localScale : t.lossyScale;
            string parentField = parentId != 0 ? $"\n      \"parent\": {parentId}," : "";

            string bgField = "";
            if (!string.IsNullOrEmpty(cameraExport.backgroundPath))
            {
                bgField = $",\n        \"prerenderedBackground\": \"{EscapeJson(cameraExport.backgroundPath)}\"";
            }

            string json = $@"    {{
      ""id"": {cameraExport.entityId},
{parentField}
      ""name"": ""{EscapeJson(cam.gameObject.name)}"",
      ""camera"": {{
        ""fov"": {cam.fieldOfView:F6},
        ""nearClip"": {cam.nearClipPlane:F6},
        ""farClip"": {cam.farClipPlane:F6},
        ""primary"": {(cameraExport.primary ? "true" : "false")}{bgField}
      }},
      ""transform"": {{
        ""position"": [{pos.x:F6}, {pos.y:F6}, {pos.z:F6}],
        ""rotation"": [{rot.x:F6}, {rot.y:F6}, {rot.z:F6}],
        ""scale"": [{scl.x:F6}, {scl.y:F6}, {scl.z:F6}]
      }}
    }}";
            return json;
        }

        private string BuildPrerenderOccluderEntityJson(
            MipsyncPrerenderOccluder occluder, int id,
            Dictionary<Transform, int> entityIdByTransform)
        {
            var mesh = GetPrerenderOccluderMesh(occluder);
            if (mesh == null)
                return "";
            var t = occluder.transform;
            int parentId = GetExportedParentId(t, entityIdByTransform);
            var pos = ToMipsyncPosition(parentId != 0 ? t.localPosition : t.position);
            var rot = ToMipsyncEuler(parentId != 0 ? t.localRotation : t.rotation);
            var scl = parentId != 0 ? t.localScale : t.lossyScale;
            string parentField = parentId != 0 ? $"\n      \"parent\": {parentId}," : "";

            var vertices = new List<PbVertex>(mesh.vertexCount);
            var meshVertices = mesh.vertices;
            var meshNormals = mesh.normals;
            for (int i = 0; i < meshVertices.Length; ++i)
            {
                Vector3 normal = meshNormals != null && i < meshNormals.Length
                    ? ToMipsyncVector(meshNormals[i]).normalized
                    : Vector3.up;
                vertices.Add(new PbVertex {
                    position = ToMipsyncVector(meshVertices[i]),
                    normal = normal,
                    uv = Vector2.zero,
                    color = Color.white
                });
            }

            // Reflecting Unity's Z axis changes winding, so reverse each triangle.
            var sourceIndices = mesh.triangles;
            var indices = new List<int>(sourceIndices.Length);
            for (int i = 0; i + 2 < sourceIndices.Length; i += 3)
            {
                indices.Add(sourceIndices[i]);
                indices.Add(sourceIndices[i + 2]);
                indices.Add(sourceIndices[i + 1]);
            }

            return $@"    {{
      ""id"": {id},
{parentField}
      ""name"": ""{EscapeJson(occluder.gameObject.name)}_PrerenderOccluder"",
      ""meshRenderer"": {{
        ""primitive"": ""ProBuilder"",
        ""size"": 1.0,
        ""enabled"": true,
        ""prerenderOccluder"": true,
        ""editorOnly"": true,
        ""color"": [1.0, 1.0, 1.0, 1.0]
      }},
{BuildProBuilderJson(vertices, indices)},
      ""transform"": {{
        ""position"": [{pos.x:F6}, {pos.y:F6}, {pos.z:F6}],
        ""rotation"": [{rot.x:F6}, {rot.y:F6}, {rot.z:F6}],
        ""scale"": [{scl.x:F6}, {scl.y:F6}, {scl.z:F6}]
      }}
    }}";
        }

        private static Mesh GetPrerenderOccluderMesh(MipsyncPrerenderOccluder occluder)
        {
            if (occluder == null)
                return null;

            var meshFilter = occluder.GetComponent<MeshFilter>();
            if (meshFilter != null && meshFilter.sharedMesh != null)
                return meshFilter.sharedMesh;

            var meshCollider = occluder.GetComponent<MeshCollider>();
            return meshCollider != null ? meshCollider.sharedMesh : null;
        }

        private string BuildBackgroundPreviewEntityJson(CameraExportData cameraExport)
        {
            Camera cam = cameraExport.camera;
            MipsyncPS1SceneExporter exporter = cameraExport.exporter;
            float distance = Mathf.Max(0.1f, exporter.backgroundPreviewDistance);
            float height = 2.0f * distance * Mathf.Tan(cam.fieldOfView * Mathf.Deg2Rad * 0.5f);
            float aspect = exporter.backgroundHeight > 0
                ? exporter.backgroundWidth / (float)exporter.backgroundHeight
                : cam.aspect;
            float width = height * Mathf.Max(0.01f, aspect);

            var vertices = new List<PbVertex>();
            var indices = new List<int>();
            Color color = Color.white;
            AppendQuad(vertices, indices,
                       new Vector3(-0.5f, -0.5f, 0.0f),
                       new Vector3( 0.5f, -0.5f, 0.0f),
                       new Vector3( 0.5f,  0.5f, 0.0f),
                       new Vector3(-0.5f,  0.5f, 0.0f),
                       Vector3.forward, color);

            return $@"    {{
      ""id"": {cameraExport.previewEntityId},
      ""parent"": {cameraExport.entityId},
      ""name"": ""{EscapeJson(cam.gameObject.name)}_PrerenderPreview"",
      ""meshRenderer"": {{
        ""primitive"": ""ProBuilder"",
        ""size"": 1.0,
        ""texture"": ""{EscapeJson(cameraExport.backgroundPath)}"",
        ""color"": [1.0, 1.0, 1.0, 1.0],
        ""tiling"": [1.0, 1.0],
        ""offset"": [0.5, 0.5],
        ""editorOnly"": true
      }},
{BuildProBuilderJson(vertices, indices)},
      ""transform"": {{
        ""position"": [0.0, 0.0, {F(-distance)}],
        ""rotation"": [0.0, 0.0, 0.0],
        ""scale"": [{F(width)}, {F(height)}, 1.0]
      }}
    }}";
        }

        private static Vector3 ToMipsyncPosition(Vector3 unityPosition)
        {
            return new Vector3(unityPosition.x, unityPosition.y, -unityPosition.z);
        }

        private static Vector3 ToMipsyncVector(Vector3 unityVector)
        {
            return new Vector3(unityVector.x, unityVector.y, -unityVector.z);
        }

        private static Vector3 ToMipsyncEuler(Transform transform)
        {
            return ToMipsyncEuler(transform.rotation);
        }

        private static Vector3 ToMipsyncEuler(Quaternion rotation)
        {
            Matrix4x4 unity = Matrix4x4.Rotate(rotation);
            Matrix4x4 mipsync = ReflectZ(unity);
            return ExtractMipsyncEuler(mipsync);
        }

        private static Matrix4x4 ReflectZ(Matrix4x4 matrix)
        {
            Matrix4x4 reflected = matrix;
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    float sign = (row == 2 ? -1.0f : 1.0f) *
                                 (col == 2 ? -1.0f : 1.0f);
                    reflected[row, col] = matrix[row, col] * sign;
                }
            }
            return reflected;
        }

        private static Vector3 ExtractMipsyncEuler(Matrix4x4 matrix)
        {
            // Mipsync composes transforms as Ry(yaw) * Rx(pitch) * Rz(roll).
            float pitchRad = Mathf.Asin(Mathf.Clamp(-matrix[1, 2], -1.0f, 1.0f));
            float cosPitch = Mathf.Cos(pitchRad);
            float yawRad;
            float rollRad;

            if (Mathf.Abs(cosPitch) > 0.0001f)
            {
                yawRad = Mathf.Atan2(matrix[0, 2], matrix[2, 2]);
                rollRad = Mathf.Atan2(matrix[1, 0], matrix[1, 1]);
            }
            else
            {
                yawRad = Mathf.Atan2(-matrix[2, 0], matrix[0, 0]);
                rollRad = 0.0f;
            }

            return new Vector3(NormalizeDegrees(pitchRad * Mathf.Rad2Deg),
                               NormalizeDegrees(yawRad * Mathf.Rad2Deg),
                               NormalizeDegrees(rollRad * Mathf.Rad2Deg));
        }

        private static float NormalizeDegrees(float degrees)
        {
            degrees = Mathf.Repeat(degrees, 360.0f);
            return degrees > 180.0f ? degrees - 360.0f : degrees;
        }

        private string BuildColliderEntityJson(
            Collider col, int id,
            Dictionary<Camera, int> cameraEntityIdByCamera,
            Dictionary<Transform, int> entityIdByTransform)
        {
            var t = col.transform;
            int parentId = GetExportedParentId(t, entityIdByTransform);
            var pos = ToMipsyncPosition(parentId != 0 ? t.localPosition : t.position);
            var rot = ToMipsyncEuler(parentId != 0 ? t.localRotation : t.rotation);
            var scl = parentId != 0 ? t.localScale : t.lossyScale;
            string parentField = parentId != 0 ? $"\n      \"parent\": {parentId}," : "";

            int shape = -1;
            Vector3 center = Vector3.zero;
            Vector3 halfExtents = Vector3.one * 0.5f;
            float radius = 0.5f;
            float capsuleHeight = 1.0f;

            if (col is BoxCollider box)
            {
                shape = 0;
                center = ToMipsyncVector(box.center);
                halfExtents = box.size * 0.5f;
            }
            else if (col is SphereCollider sphere)
            {
                shape = 1;
                center = ToMipsyncVector(sphere.center);
                radius = sphere.radius;
                halfExtents = Vector3.one * radius;
            }
            else if (col is CapsuleCollider capsule)
            {
                shape = 2;
                center = ToMipsyncVector(capsule.center);
                radius = capsule.radius;
                capsuleHeight = capsule.height;
                halfExtents = new Vector3(radius, capsuleHeight * 0.5f, radius);
            }
            else if (col is CharacterController character)
            {
                shape = 2;
                center = ToMipsyncVector(character.center);
                radius = character.radius;
                capsuleHeight = character.height;
                halfExtents = new Vector3(radius, capsuleHeight * 0.5f, radius);
            }
            else if (col is MeshCollider meshCollider)
            {
                // MeshCollider exported as shape=3 (mesh-plane) placeholder.
                shape = 3;
                if (meshCollider.sharedMesh != null)
                {
                    // sharedMesh.bounds is already in the collider's local space.
                    // Transform scale is exported separately, so do not bake it twice.
                    var localBounds = meshCollider.sharedMesh.bounds;
                    center = ToMipsyncVector(localBounds.center);
                    halfExtents = localBounds.extents;
                }
                else
                {
                    // Missing mesh fallback. Convert world bounds approximately while
                    // guarding zero/negative scale instead of collapsing to 1x1.
                    var worldBounds = col.bounds;
                    var lossy = t.lossyScale;
                    center = ToMipsyncVector(t.InverseTransformPoint(worldBounds.center));
                    halfExtents = new Vector3(
                        worldBounds.extents.x / Mathf.Max(Mathf.Abs(lossy.x), 0.0001f),
                        worldBounds.extents.y / Mathf.Max(Mathf.Abs(lossy.y), 0.0001f),
                        worldBounds.extents.z / Mathf.Max(Mathf.Abs(lossy.z), 0.0001f));
                }
            }
            else
            {
                return ""; // Unsupported collider type, skip.
            }

            var cameraZone = col.GetComponent<MipsyncCameraZone>();
            bool cameraTrigger = IsCameraTriggerCollider(col) || cameraZone != null;
            int cameraTargetEntityId = 0;
            if (cameraZone != null)
            {
                if (cameraZone.targetCamera != null &&
                    cameraEntityIdByCamera.TryGetValue(cameraZone.targetCamera, out int targetId))
                {
                    cameraTargetEntityId = targetId;
                }
                else
                {
                    Debug.LogWarning("[MipsyncPS1SceneExporter] Camera Zone has no exported target camera: " +
                                     GetHierarchyPath(col.transform));
                }
            }
            string cameraTargetJson = cameraTargetEntityId > 0
                ? $",\n        \"cameraTarget\": {cameraTargetEntityId}"
                : "";

            string json = $@"    {{
      ""id"": {id},
{parentField}
      ""name"": ""{EscapeJson(col.gameObject.name)}"",
      ""unityTag"": ""{EscapeJson(col.gameObject.tag)}"",
      ""collider"": {{
        ""shape"": {shape},
        ""center"": [{center.x:F6}, {center.y:F6}, {center.z:F6}],
        ""halfExtents"": [{halfExtents.x:F6}, {halfExtents.y:F6}, {halfExtents.z:F6}],
        ""radius"": {radius:F6},
        ""capsuleHeight"": {capsuleHeight:F6},
        ""isTrigger"": {(col.isTrigger || cameraTrigger ? "true" : "false")},
        ""cameraTrigger"": {(cameraTrigger ? "true" : "false")}{cameraTargetJson}
      }},
      ""transform"": {{
        ""position"": [{pos.x:F6}, {pos.y:F6}, {pos.z:F6}],
        ""rotation"": [{rot.x:F6}, {rot.y:F6}, {rot.z:F6}],
        ""scale"": [{scl.x:F6}, {scl.y:F6}, {scl.z:F6}]
      }}
    }}";
            return json;
        }

        private static bool IsSupportedCollider(Collider col)
        {
            return col is BoxCollider || col is SphereCollider ||
                   col is CapsuleCollider || col is CharacterController ||
                   col is MeshCollider;
        }

        private static bool IsExpObject(GameObject go)
        {
            Transform current = go != null ? go.transform : null;
            while (current != null)
            {
                if (current.gameObject.tag == "ExpObject")
                    return true;
                current = current.parent;
            }
            return false;
        }

        private bool IsCameraTriggerCollider(Collider col)
        {
            if (col == null || col.gameObject == null)
                return false;
            string tag = col.gameObject.tag;
            return !string.IsNullOrEmpty(tag) &&
                   (tag == cameraTriggerTag ||
                    tag == "MipsyncCameraTrigger" ||
                    tag == "MipsyncShotTrigger" ||
                    tag == "CameraTrigger" ||
                    tag == "ShotTrigger");
        }

        private struct PbVertex
        {
            public Vector3 position;
            public Vector3 normal;
            public Vector2 uv;
            public Color color;
        }

        private static void AppendQuad(List<PbVertex> vertices, List<int> indices,
                                       Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                                       Vector3 normal, Color color)
        {
            int start = vertices.Count;
            vertices.Add(new PbVertex { position = a, normal = normal, uv = new Vector2(0, 0), color = color });
            vertices.Add(new PbVertex { position = b, normal = normal, uv = new Vector2(1, 0), color = color });
            vertices.Add(new PbVertex { position = c, normal = normal, uv = new Vector2(1, 1), color = color });
            vertices.Add(new PbVertex { position = d, normal = normal, uv = new Vector2(0, 1), color = color });
            indices.Add(start + 0); indices.Add(start + 1); indices.Add(start + 2);
            indices.Add(start + 0); indices.Add(start + 2); indices.Add(start + 3);
        }

        private static string BuildProBuilderJson(List<PbVertex> vertices, List<int> indices)
        {
            var sb = new StringBuilder();
            sb.AppendLine("      \"proBuilder\": {");
            sb.AppendLine("        \"shape\": 4,");
            sb.AppendLine("        \"size\": [1.0, 1.0, 1.0],");
            sb.AppendLine("        \"steps\": 4,");
            sb.AppendLine("        \"extrudeAmount\": 0.25,");
            sb.AppendLine("        \"vertices\": [");
            for (int i = 0; i < vertices.Count; ++i)
            {
                PbVertex v = vertices[i];
                sb.Append("          { \"position\": [")
                  .Append(F(v.position.x)).Append(", ").Append(F(v.position.y)).Append(", ").Append(F(v.position.z))
                  .Append("], \"normal\": [")
                  .Append(F(v.normal.x)).Append(", ").Append(F(v.normal.y)).Append(", ").Append(F(v.normal.z))
                  .Append("], \"uv\": [")
                  .Append(F(v.uv.x)).Append(", ").Append(F(v.uv.y))
                  .Append("], \"color\": [")
                  .Append(F(v.color.r)).Append(", ").Append(F(v.color.g)).Append(", ")
                  .Append(F(v.color.b)).Append(", ").Append(F(v.color.a)).Append("] }");
                sb.AppendLine(i + 1 < vertices.Count ? "," : "");
            }
            sb.AppendLine("        ],");
            sb.Append("        \"indices\": [");
            for (int i = 0; i < indices.Count; ++i)
            {
                if (i > 0)
                    sb.Append(", ");
                sb.Append(indices[i]);
            }
            sb.AppendLine("]");
            sb.Append("      }");
            return sb.ToString();
        }

        private static string F(float value)
        {
            return value.ToString("F6", CultureInfo.InvariantCulture);
        }

        private static string MakeSafeFileName(string value)
        {
            if (string.IsNullOrEmpty(value))
                return "scene";

            char[] invalid = Path.GetInvalidFileNameChars();
            var sb = new StringBuilder(value.Length);
            foreach (char c in value)
            {
                bool bad = false;
                for (int i = 0; i < invalid.Length; ++i)
                {
                    if (c == invalid[i])
                    {
                        bad = true;
                        break;
                    }
                }
                sb.Append(bad ? '_' : c);
            }
            return sb.ToString();
        }

        private string BuildLightEntityJson(
            Light light, int id, Dictionary<Transform, int> entityIdByTransform)
        {
            var t = light.transform;
            int parentId = GetExportedParentId(t, entityIdByTransform);
            var pos = ToMipsyncPosition(parentId != 0 ? t.localPosition : t.position);
            var rot = ToMipsyncEuler(parentId != 0 ? t.localRotation : t.rotation);
            var scl = parentId != 0 ? t.localScale : t.lossyScale;
            var col = light.color;
            string parentField = parentId != 0 ? $"\n      \"parent\": {parentId}," : "";

            string json = $@"    {{
      ""id"": {id},
{parentField}
      ""name"": ""{EscapeJson(light.gameObject.name)}"",
      ""light"": {{
        ""type"": 0,
        ""color"": [{col.r:F6}, {col.g:F6}, {col.b:F6}],
        ""intensity"": {light.intensity:F6},
        ""range"": {light.range:F6},
        ""spotAngle"": 45.0,
        ""spotInnerAngle"": 30.0
      }},
      ""transform"": {{
        ""position"": [{pos.x:F6}, {pos.y:F6}, {pos.z:F6}],
        ""rotation"": [{rot.x:F6}, {rot.y:F6}, {rot.z:F6}],
        ""scale"": [{scl.x:F6}, {scl.y:F6}, {scl.z:F6}]
      }}
    }}";
            return json;
        }

        private static int GetExportedParentId(
            Transform transform, Dictionary<Transform, int> entityIdByTransform)
        {
            if (transform == null || transform.parent == null)
                return 0;
            return entityIdByTransform.TryGetValue(transform.parent, out int parentId)
                ? parentId
                : 0;
        }

        private static string EscapeJson(string s)
        {
            if (s == null) return "";
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"")
                    .Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t");
        }
    }
}
