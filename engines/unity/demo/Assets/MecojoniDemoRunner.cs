// SPDX-License-Identifier: MPL-2.0
//
// The demo batch, shared between play mode (MecojoniDemo) and the headless
// editor verification (Editor/MecojoniBatch): compile hello.meco, round-trip
// a bytecode/1 artifact, reject a corrupted artifact, and compile the
// multi-module npc package with a playerName input. Seeds are fixed, so the
// generated `hello seed N -> ...` / `npc seed N -> ...` lines must match the
// Godot and Defold demos and the Rust/C tests exactly (the surrounding
// framing lines differ per engine).

using System;
using System.Collections.Generic;
using System.IO;
using Mecojoni;
using UnityEngine;

public static class MecojoniDemoRunner
{
    public static string DataPath(string relative)
    {
        return Path.Combine(Application.streamingAssetsPath, relative);
    }

    /// <summary>Loads the single-module hello grammar, proving the artifact
    /// path on the way: encode in-memory, reload, and compare. A game would
    /// normally ship `.mecob` files precompiled with the authoring CLI and
    /// call MecoGrammar.LoadArtifact(File.ReadAllBytes(...)) directly.</summary>
    public static MecoGrammar LoadHello(Action<string> log)
    {
        var source = MecoGrammar.Compile("hello.meco", File.ReadAllText(DataPath("hello.meco")));
        log("entries: " + string.Join(", ", source.Entries) +
            " | default: " + source.DefaultEntry);
        foreach (string warning in source.Warnings)
        {
            log("warning: " + warning);
        }

        byte[] artifact = source.EncodeArtifact(0);
        var reloaded = MecoGrammar.LoadArtifact(artifact);
        bool match = source.Generate(42).Text == reloaded.Generate(42).Text;
        log(string.Format("bytecode/1 artifact round-trip OK ({0} bytes), match={1}",
            artifact.Length, match ? "true" : "false"));

        // Hostile input must fail cleanly with a stable code, never crash.
        byte[] corrupt = (byte[])artifact.Clone();
        for (int i = 0; i < Math.Min(64, corrupt.Length); i += 1)
        {
            corrupt[i] = 0xFF;
        }
        try
        {
            MecoGrammar.LoadArtifact(corrupt);
            log("corrupt artifact was NOT rejected -- this is a bug");
        }
        catch (MecoException error)
        {
            log("corrupt artifact rejected: " + error.Code);
        }

        source.Dispose();
        return reloaded;
    }

    /// <summary>Compiles the multi-module npc package.</summary>
    public static MecoGrammar LoadNpc(Action<string> log)
    {
        using (var builder = new MecoPackageBuilder())
        {
            builder.AddModule("root", "root.meco", File.ReadAllText(DataPath(Path.Combine("npc", "root.meco"))));
            builder.AddModule("common", "common.meco", File.ReadAllText(DataPath(Path.Combine("npc", "common.meco"))));
            builder.ResolveImport("root", "./common.meco", "common");
            return builder.Compile();
        }
    }

    public static void Generate(Action<string> log, string label, MecoGrammar grammar,
                                ulong seed, IReadOnlyDictionary<string, MecoValue> data)
    {
        MecoGenerated result = grammar.Generate(seed, null, data);
        if (result.Ok)
        {
            log(string.Format("{0} seed {1} -> {2}", label, seed, result.Text));
        }
        else
        {
            log(string.Format("{0} ERROR {1}: {2}", label, result.ErrorCode, result.ErrorMessage));
        }
    }

    public static IReadOnlyDictionary<string, MecoValue> NpcData(string playerName)
    {
        return new Dictionary<string, MecoValue> { { "playerName", playerName } };
    }

    /// <summary>The full fixed-seed batch. Throws MecoException on any load
    /// or compile failure.</summary>
    public static void RunBatch(Action<string> log)
    {
        log(string.Format("Mecojoni versions: ffi_abi={0} core_api={1} bytecode={2}",
            MecoVersions.FfiAbi, MecoVersions.CoreApi, MecoVersions.Bytecode));

        using (MecoGrammar hello = LoadHello(log))
        {
            for (ulong seed = 0; seed <= 4; seed += 1)
            {
                Generate(log, "hello", hello, seed, null);
            }
        }
        using (MecoGrammar npc = LoadNpc(log))
        {
            var data = NpcData("Ripley");
            for (ulong seed = 0; seed <= 2; seed += 1)
            {
                Generate(log, "npc", npc, seed, data);
            }
        }
        log("MECOJONI DEMO DONE");
    }
}
