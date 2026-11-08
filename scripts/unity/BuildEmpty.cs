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
// Output defaults to <project>/stock-player/empty.exe; override with
// WA2DC_STOCK_PLAYER_OUT.
public static class BuildEmpty
{
    public static void Build()
    {
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
