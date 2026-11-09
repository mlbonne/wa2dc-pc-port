using System;
using System.IO;
using UnityEditor;
using UnityEngine;

// Editor-side helper for Phase 2: build a pristine Unity 3.5.6f4 Windows
// standalone player from an empty project. This only supplies the stock
// player + _Data template; the game data is overlaid afterwards.
//
// Place in <project>/Assets/Editor/BuildEmpty.cs and run:
//   Unity.exe -batchmode -quit -projectPath <project> `
//     -executeMethod BuildEmpty.Build -logFile <log>
//
// The build script copies the game's app_icon.png into Assets/ first; this
// helper applies it as the Standalone player icon before building.
//
// Output defaults to <project>/stock-player/empty.exe; override with
// WA2DC_STOCK_PLAYER_OUT.
public static class BuildEmpty
{
    private static void ApplyIcon()
    {
        const string iconPath = "Assets/app_icon.png";
        AssetDatabase.ImportAsset(iconPath, ImportAssetOptions.ForceUpdate);
        TextureImporter importer = AssetImporter.GetAtPath(iconPath) as TextureImporter;
        if (importer != null)
        {
            importer.mipmapEnabled = false;
            importer.maxTextureSize = 256;
            AssetDatabase.WriteImportSettingsIfDirty(iconPath);
            AssetDatabase.ImportAsset(iconPath, ImportAssetOptions.ForceUpdate);
        }
        Texture2D icon = AssetDatabase.LoadAssetAtPath(iconPath, typeof(Texture2D)) as Texture2D;
        if (icon == null)
        {
            Debug.LogWarning("app_icon.png was not imported; using the stock Unity icon");
            return;
        }
        int[] sizes = PlayerSettings.GetIconSizesForTargetGroup(BuildTargetGroup.Standalone);
        Texture2D[] icons = new Texture2D[sizes.Length];
        for (int i = 0; i < icons.Length; i++) icons[i] = icon;
        PlayerSettings.SetIconsForTargetGroup(BuildTargetGroup.Standalone, icons);
        Debug.Log("Set Standalone icon from " + iconPath + " (" + icon.width + "x" + icon.height +
                  ", " + icons.Length + " slots)");
    }

    public static void Build()
    {
        ApplyIcon();
        EditorApplication.NewScene();
        const string scenePath = "Assets/Empty.unity";
        EditorApplication.SaveScene(scenePath);
        AssetDatabase.Refresh();

        string output = Environment.GetEnvironmentVariable("WA2DC_STOCK_PLAYER_OUT");
        if (string.IsNullOrEmpty(output))
        {
            string projectRoot = Path.GetFullPath(Path.Combine(Application.dataPath, ".."));
            output = Path.Combine(Path.Combine(projectRoot, "stock-player"), "empty.exe");
        }
        Directory.CreateDirectory(Path.GetDirectoryName(output));
        BuildPipeline.BuildPlayer(
            new string[] { scenePath },
            output,
            BuildTarget.StandaloneWindows,
            BuildOptions.None);
        Debug.Log("Built stock player: " + output);
    }
}
