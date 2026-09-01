/*
 * MipsyncPS1SceneExporterEditor.cs
 *
 * Custom inspector UI for MipsyncPS1SceneExporter.
 * Must reside inside an 'Editor' folder in Unity.
 */

using UnityEngine;
using UnityEditor;

namespace Mipsync
{
    [CustomEditor(typeof(MipsyncPS1SceneExporter))]
    public class MipsyncPS1SceneExporterEditor : Editor
    {
        public override void OnInspectorGUI()
        {
            DrawDefaultInspector();

            var exporter = (MipsyncPS1SceneExporter)target;

            EditorGUILayout.Space(10);
            EditorGUILayout.LabelField("", GUI.skin.horizontalSlider);
            EditorGUILayout.Space(5);

            // Big export button.
            var prevColor = GUI.backgroundColor;
            GUI.backgroundColor = new Color(0.3f, 0.8f, 0.4f, 1f);

            GUIStyle buttonStyle = new GUIStyle(GUI.skin.button)
            {
                fontSize = 14,
                fontStyle = FontStyle.Bold,
                fixedHeight = 40
            };

            if (GUILayout.Button("▶  Export PS1 Scene", buttonStyle))
            {
                exporter.ExportScene();
            }

            GUI.backgroundColor = prevColor;

            EditorGUILayout.Space(5);

            // Info box.
            EditorGUILayout.HelpBox(
                "Exports cameras, pre-rendered backgrounds, and ExpObject-tagged colliders to a .nscene file " +
                "compatible with MipsyncEngine's PS1 pipeline.\n\n" +
                "Attach this component to your main Camera.\n" +
                "Camera switching trigger volumes are always exported, regardless of ExpObject, layer mask, " +
                "or Include Colliders.\n" +
                "Press 'Export PS1 Scene' to generate all assets.",
                MessageType.Info);

            // Quick preview info.
            if (exporter.captureBackground)
            {
                EditorGUILayout.LabelField("Background",
                    $"{exporter.backgroundWidth} × {exporter.backgroundHeight} PNG");
            }

            if (exporter.includeColliders)
            {
                int colliderCount = 0;
                foreach (var collider in FindObjectsOfType<Collider>())
                {
                    if (collider != null && collider.gameObject.tag == "ExpObject")
                        colliderCount++;
                }
                EditorGUILayout.LabelField("ExpObject Colliders", colliderCount.ToString());
            }
        }
    }
}
