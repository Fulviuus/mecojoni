// SPDX-License-Identifier: MPL-2.0
//
// Headless verification entry point, mirroring the Godot --headless and
// Defold dmengine checks. Run from the command line (no play mode needed --
// this exercises the same native library and wrapper the game uses):
//
//   Unity -batchmode -nographics -projectPath engines/unity/demo \
//         -executeMethod MecojoniBatch.Run -logFile -
//
// Prints the fixed-seed batch and exits 0, or logs the failure and exits 1.

using System;
using UnityEditor;
using UnityEngine;

public static class MecojoniBatch
{
    public static void Run()
    {
        try
        {
            MecojoniDemoRunner.RunBatch(Debug.Log);
            Debug.Log("MECOJONI UNITY BATCH OK");
            EditorApplication.Exit(0);
        }
        catch (Exception error)
        {
            Debug.LogError("MECOJONI UNITY BATCH FAILED: " + error);
            EditorApplication.Exit(1);
        }
    }
}
