// SPDX-License-Identifier: MPL-2.0
//
// Public managed API over the Mecojoni C ABI.
//
// Architecture: mecojoni-core (no_std Rust) -> mecojoni-ffi (C ABI) -> this
// P/Invoke wrapper -> your game code. The C ABI is engine-agnostic and shared
// with the Godot and Defold integrations; this layer only translates between
// .NET values and the C contract. It deliberately has no UnityEngine
// dependency, so it can be exercised by any .NET host.
//
// Conventions:
//  - Loading and compiling throw MecoException (stable E_* code + rendered
//    diagnostics) on failure; Generate returns a MecoGenerated result object
//    instead of throwing, mirroring the Godot/Defold demo APIs.
//  - MecoGrammar and MecoPackageBuilder own native memory: dispose them (or
//    let finalizers reclaim them). One instance must not be used from two
//    threads at once; distinct instances are independent.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Mecojoni
{
    /// <summary>Failure from the Mecojoni runtime, carrying the stable
    /// diagnostic code of the first failure (e.g. "E_BYTECODE_CORRUPT") and
    /// every diagnostic rendered one per line.</summary>
    public sealed class MecoException : Exception
    {
        public string Code { get; }

        public MecoException(string code, string message)
            : base(code + ": " + message)
        {
            Code = code;
        }
    }

    /// <summary>One typed host input for generation. Mirrors the compiled
    /// input schema's types; construct via the static factories or the
    /// implicit conversions from string / bool / int / long. Fractional
    /// floats are rejected by design: generation is exact and replayable, so
    /// non-integers must be spelled as rationals.</summary>
    public readonly struct MecoValue
    {
        internal readonly uint Kind;
        internal readonly string TextValue;
        internal readonly long Numerator;
        internal readonly ulong Denominator;
        internal readonly bool BooleanValue;

        private MecoValue(uint kind, string text, long numerator, ulong denominator, bool boolean)
        {
            Kind = kind;
            TextValue = text;
            Numerator = numerator;
            Denominator = denominator;
            BooleanValue = boolean;
        }

        public static MecoValue Text(string value)
        {
            if (value == null) throw new ArgumentNullException(nameof(value));
            return new MecoValue(MecojoniNative.ValueText, value, 0, 1, false);
        }

        /// <summary>A finite enum member; its enum type is determined by the
        /// compiled schema.</summary>
        public static MecoValue EnumMember(string member)
        {
            if (member == null) throw new ArgumentNullException(nameof(member));
            return new MecoValue(MecojoniNative.ValueEnum, member, 0, 1, false);
        }

        public static MecoValue Boolean(bool value)
        {
            return new MecoValue(MecojoniNative.ValueBoolean, null, 0, 1, value);
        }

        public static MecoValue Integer(long value)
        {
            return new MecoValue(MecojoniNative.ValueNumber, null, value, 1, false);
        }

        /// <summary>An exact rational number.</summary>
        public static MecoValue Rational(long numerator, ulong denominator)
        {
            if (denominator == 0)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(denominator), "denominator must be positive");
            }
            return new MecoValue(MecojoniNative.ValueNumber, null, numerator, denominator, false);
        }

        public static implicit operator MecoValue(string value) => Text(value);
        public static implicit operator MecoValue(bool value) => Boolean(value);
        public static implicit operator MecoValue(int value) => Integer(value);
        public static implicit operator MecoValue(long value) => Integer(value);
    }

    /// <summary>Result of one deterministic weighted generation.</summary>
    public sealed class MecoGenerated
    {
        public bool Ok { get; internal set; }
        public string Text { get; internal set; }
        public string Entry { get; internal set; }
        public uint Expansions { get; internal set; }
        public uint SamplerWords { get; internal set; }
        public string ErrorCode { get; internal set; }
        public string ErrorMessage { get; internal set; }
    }

    /// <summary>Build metadata of the linked native library.</summary>
    public static class MecoVersions
    {
        public static uint FfiAbi => MecojoniNative.meco_ffi_abi_version();
        public static uint CoreApi => MecojoniNative.meco_core_api_version();
        public static string Bytecode =>
            MecojoniNative.ViewToString(MecojoniNative.meco_bytecode_version());
    }

    /// <summary>One compiled Mecojoni grammar (the runtime VM handle). Load
    /// from `.meco` source with <see cref="Compile"/>, from compiled
    /// `bytecode/1` artifacts with <see cref="LoadArtifact"/>, or from
    /// <see cref="MecoPackageBuilder"/> for multi-module packages.</summary>
    public sealed class MecoGrammar : IDisposable
    {
        private IntPtr handle;

        private MecoGrammar(IntPtr handle)
        {
            this.handle = handle;
        }

        ~MecoGrammar()
        {
            ReleaseHandle();
        }

        public void Dispose()
        {
            ReleaseHandle();
            GC.SuppressFinalize(this);
        }

        private void ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
            {
                MecojoniNative.meco_grammar_free(handle);
                handle = IntPtr.Zero;
            }
        }

        private IntPtr Handle
        {
            get
            {
                if (handle == IntPtr.Zero)
                {
                    throw new ObjectDisposedException(nameof(MecoGrammar));
                }
                return handle;
            }
        }

        internal static MecoGrammar Adopt(IntPtr owned)
        {
            return new MecoGrammar(owned);
        }

        private static MecoException ErrorFrom(IntPtr error, int status)
        {
            string fallback = status == MecojoniNative.StatusInvalidArgument
                ? "E_INVALID_ARGUMENT" : "E_UNKNOWN";
            MecojoniNative.ConsumeError(error, fallback, out var code, out var message);
            return new MecoException(code, message);
        }

        /// <summary>Compiles one importless `.meco` module. Throws
        /// <see cref="MecoException"/> with the compiler diagnostics on
        /// failure.</summary>
        public static MecoGrammar Compile(string name, string source)
        {
            if (name == null) throw new ArgumentNullException(nameof(name));
            if (source == null) throw new ArgumentNullException(nameof(source));
            byte[] sourceUtf8 = System.Text.Encoding.UTF8.GetBytes(source);
            int status = MecojoniNative.meco_grammar_compile(
                MecojoniNative.Utf8z(name), sourceUtf8,
                (UIntPtr)(ulong)sourceUtf8.Length, out IntPtr grammar, out IntPtr error);
            if (status != MecojoniNative.StatusOk)
            {
                throw ErrorFrom(error, status);
            }
            return new MecoGrammar(grammar);
        }

        /// <summary>Decodes and verifies a compiled `bytecode/1` artifact
        /// under the format's hostile-input limits. No partial grammar is
        /// ever returned; corrupt input throws with a stable E_* code.</summary>
        public static MecoGrammar LoadArtifact(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException(nameof(bytes));
            int status = MecojoniNative.meco_grammar_decode(
                bytes, (UIntPtr)(ulong)bytes.Length, out IntPtr grammar, out IntPtr error);
            if (status != MecojoniNative.StatusOk)
            {
                throw ErrorFrom(error, status);
            }
            return new MecoGrammar(grammar);
        }

        /// <summary>Encodes this grammar to `bytecode/1` bytes; profile is
        /// 0 full, 1 mapped, 2 stripped.</summary>
        public byte[] EncodeArtifact(uint debugProfile = 0)
        {
            // GC.KeepAlive in the finally blocks below (and in every member
            // touching the handle): without it the JIT may consider `this`
            // unreachable after the last field read, letting the finalizer
            // free the native object while the native call is still running
            // or while a borrowed MecoStrView is being copied.
            try
            {
                int status = MecojoniNative.meco_grammar_encode(
                    Handle, debugProfile, out IntPtr artifact, out IntPtr error);
                if (status != MecojoniNative.StatusOk)
                {
                    throw ErrorFrom(error, status);
                }
                int length = checked((int)MecojoniNative.meco_artifact_len(artifact).ToUInt64());
                byte[] bytes = new byte[length];
                if (length > 0)
                {
                    Marshal.Copy(MecojoniNative.meco_artifact_data(artifact), bytes, 0, length);
                }
                MecojoniNative.meco_artifact_free(artifact);
                return bytes;
            }
            finally
            {
                GC.KeepAlive(this);
            }
        }

        /// <summary>Qualified public entry names, e.g. "hello.greeting".</summary>
        public string[] Entries
        {
            get
            {
                try
                {
                    int count = checked((int)MecojoniNative.meco_grammar_entry_count(Handle).ToUInt64());
                    var entries = new string[count];
                    for (int i = 0; i < count; i += 1)
                    {
                        entries[i] = MecojoniNative.ViewToString(
                            MecojoniNative.meco_grammar_entry(Handle, (UIntPtr)(ulong)i));
                    }
                    return entries;
                }
                finally
                {
                    GC.KeepAlive(this);
                }
            }
        }

        /// <summary>The package's default entry, or null when none is
        /// declared.</summary>
        public string DefaultEntry
        {
            get
            {
                try
                {
                    return MecojoniNative.ViewToString(
                        MecojoniNative.meco_grammar_default_entry(Handle));
                }
                finally
                {
                    GC.KeepAlive(this);
                }
            }
        }

        public int RuleCount
        {
            get
            {
                try
                {
                    return checked((int)MecojoniNative.meco_grammar_rule_count(Handle).ToUInt64());
                }
                finally
                {
                    GC.KeepAlive(this);
                }
            }
        }

        public int ProductionCount
        {
            get
            {
                try
                {
                    return checked((int)MecojoniNative.meco_grammar_production_count(Handle).ToUInt64());
                }
                finally
                {
                    GC.KeepAlive(this);
                }
            }
        }

        /// <summary>Compiler warnings as one "CODE: message" line each.</summary>
        public string[] Warnings
        {
            get
            {
                try
                {
                    int count = checked((int)MecojoniNative.meco_grammar_warning_count(Handle).ToUInt64());
                    var warnings = new string[count];
                    for (int i = 0; i < count; i += 1)
                    {
                        UIntPtr index = (UIntPtr)(ulong)i;
                        warnings[i] =
                            MecojoniNative.ViewToString(MecojoniNative.meco_grammar_warning_code(Handle, index))
                            + ": "
                            + MecojoniNative.ViewToString(MecojoniNative.meco_grammar_warning_message(Handle, index));
                    }
                    return warnings;
                }
                finally
                {
                    GC.KeepAlive(this);
                }
            }
        }

        /// <summary>One stateless deterministic weighted generation: the same
        /// seed against the same grammar always produces the same text.
        /// `entry` null or "" selects the package's default entry; `data`
        /// supplies declared `inputs:`.</summary>
        public MecoGenerated Generate(
            ulong seed,
            string entry = null,
            IReadOnlyDictionary<string, MecoValue> data = null)
        {
            IntPtr grammar = Handle;
            int bindingCount = data?.Count ?? 0;

            // Pin every UTF-8 payload plus the binding array for the call;
            // the ABI reads them only during meco_grammar_generate.
            var pins = new List<GCHandle>(2 * bindingCount + 2);
            try
            {
                var bindings = new MecojoniNative.MecoDataBinding[bindingCount];
                if (bindingCount > 0)
                {
                    int i = 0;
                    foreach (var pair in data)
                    {
                        if (pair.Key == null)
                        {
                            return InputError("data keys must be non-null input names");
                        }
                        byte[] nameZ = MecojoniNative.Utf8z(pair.Key);
                        var namePin = GCHandle.Alloc(nameZ, GCHandleType.Pinned);
                        pins.Add(namePin);
                        bindings[i].Name = namePin.AddrOfPinnedObject();

                        MecoValue value = pair.Value;
                        bindings[i].Kind = value.Kind;
                        bindings[i].Numerator = value.Numerator;
                        bindings[i].Denominator = value.Denominator;
                        bindings[i].Boolean = value.BooleanValue ? (byte)1 : (byte)0;
                        // Default-constructed MecoValue (e.g. `default`) has a
                        // null text payload with Kind == Text; reject it here
                        // rather than passing NULL through the ABI.
                        if (value.Kind == MecojoniNative.ValueText ||
                            value.Kind == MecojoniNative.ValueEnum)
                        {
                            if (value.TextValue == null)
                            {
                                return InputError(
                                    "input `" + pair.Key + "` has no payload; construct " +
                                    "values via the MecoValue factories");
                            }
                            byte[] textZ = MecojoniNative.Utf8z(value.TextValue);
                            var textPin = GCHandle.Alloc(textZ, GCHandleType.Pinned);
                            pins.Add(textPin);
                            bindings[i].Text = textPin.AddrOfPinnedObject();
                        }
                        else
                        {
                            bindings[i].Text = IntPtr.Zero;
                        }
                        if (value.Kind == MecojoniNative.ValueNumber && value.Denominator == 0)
                        {
                            return InputError(
                                "input `" + pair.Key + "` has no payload; construct " +
                                "values via the MecoValue factories");
                        }
                        i += 1;
                    }
                }

                var options = new MecojoniNative.MecoGenerateOptions
                {
                    Entry = IntPtr.Zero,
                    Data = IntPtr.Zero,
                    DataCount = (UIntPtr)(ulong)bindingCount,
                    Seed = seed,
                    MaxDepth = 0,
                    MaxExpansions = 0,
                    MaxOutputScalars = 0,
                    MaxOutputBytes = 0,
                    MaxSamplerWords = 0,
                };

                if (!string.IsNullOrEmpty(entry))
                {
                    byte[] entryZ = MecojoniNative.Utf8z(entry);
                    var entryPin = GCHandle.Alloc(entryZ, GCHandleType.Pinned);
                    pins.Add(entryPin);
                    options.Entry = entryPin.AddrOfPinnedObject();
                }
                if (bindingCount > 0)
                {
                    var bindingsPin = GCHandle.Alloc(bindings, GCHandleType.Pinned);
                    pins.Add(bindingsPin);
                    options.Data = bindingsPin.AddrOfPinnedObject();
                }

                int status = MecojoniNative.meco_grammar_generate(
                    grammar, ref options, out IntPtr generated, out IntPtr error);
                var result = new MecoGenerated();
                if (status != MecojoniNative.StatusOk)
                {
                    string fallback = status == MecojoniNative.StatusInvalidArgument
                        ? "E_INVALID_ARGUMENT" : "E_UNKNOWN";
                    MecojoniNative.ConsumeError(error, fallback, out var code, out var message);
                    result.Ok = false;
                    result.ErrorCode = code;
                    result.ErrorMessage = message;
                    return result;
                }
                result.Ok = true;
                result.Text = MecojoniNative.ViewToString(MecojoniNative.meco_generated_text(generated));
                result.Entry = MecojoniNative.ViewToString(MecojoniNative.meco_generated_entry(generated));
                result.Expansions = MecojoniNative.meco_generated_expansions(generated);
                result.SamplerWords = MecojoniNative.meco_generated_sampler_words(generated);
                MecojoniNative.meco_generated_free(generated);
                return result;
            }
            finally
            {
                foreach (var pin in pins)
                {
                    pin.Free();
                }
                // Keep the wrapper reachable until the native call and every
                // borrowed-view copy above have finished (see EncodeArtifact).
                GC.KeepAlive(this);
            }
        }

        private static MecoGenerated InputError(string message)
        {
            return new MecoGenerated
            {
                Ok = false,
                ErrorCode = "E_INPUT",
                ErrorMessage = message,
            };
        }
    }

    /// <summary>Accumulates the modules of one multi-module Mecojoni package.
    /// Mirrors the core's I/O-free contract: the host supplies every module
    /// and resolves every authored `imports:` edge explicitly.</summary>
    public sealed class MecoPackageBuilder : IDisposable
    {
        private IntPtr handle;

        public MecoPackageBuilder()
        {
            handle = MecojoniNative.meco_package_builder_new();
        }

        ~MecoPackageBuilder()
        {
            ReleaseHandle();
        }

        public void Dispose()
        {
            ReleaseHandle();
            GC.SuppressFinalize(this);
        }

        private void ReleaseHandle()
        {
            if (handle != IntPtr.Zero)
            {
                MecojoniNative.meco_package_builder_free(handle);
                handle = IntPtr.Zero;
            }
        }

        private IntPtr Handle
        {
            get
            {
                if (handle == IntPtr.Zero)
                {
                    throw new ObjectDisposedException(nameof(MecoPackageBuilder));
                }
                return handle;
            }
        }

        private static void ThrowFrom(IntPtr error, int status)
        {
            string fallback = status == MecojoniNative.StatusInvalidArgument
                ? "E_INVALID_ARGUMENT" : "E_UNKNOWN";
            MecojoniNative.ConsumeError(error, fallback, out var code, out var message);
            throw new MecoException(code, message);
        }

        public void AddModule(string canonicalId, string displayName, string source)
        {
            if (canonicalId == null) throw new ArgumentNullException(nameof(canonicalId));
            if (displayName == null) throw new ArgumentNullException(nameof(displayName));
            if (source == null) throw new ArgumentNullException(nameof(source));
            byte[] sourceUtf8 = System.Text.Encoding.UTF8.GetBytes(source);
            try
            {
                int status = MecojoniNative.meco_package_builder_add_module(
                    Handle, MecojoniNative.Utf8z(canonicalId), MecojoniNative.Utf8z(displayName),
                    sourceUtf8, (UIntPtr)(ulong)sourceUtf8.Length, out IntPtr error);
                if (status != MecojoniNative.StatusOk)
                {
                    ThrowFrom(error, status);
                }
            }
            finally
            {
                GC.KeepAlive(this); // see MecoGrammar.EncodeArtifact
            }
        }

        /// <summary>Resolves one authored `imports:` path of
        /// `moduleCanonicalId` to the canonical ID of another added module.</summary>
        public void ResolveImport(string moduleCanonicalId, string authoredPath, string targetCanonicalId)
        {
            if (moduleCanonicalId == null) throw new ArgumentNullException(nameof(moduleCanonicalId));
            if (authoredPath == null) throw new ArgumentNullException(nameof(authoredPath));
            if (targetCanonicalId == null) throw new ArgumentNullException(nameof(targetCanonicalId));
            try
            {
                int status = MecojoniNative.meco_package_builder_resolve_import(
                    Handle, MecojoniNative.Utf8z(moduleCanonicalId),
                    MecojoniNative.Utf8z(authoredPath), MecojoniNative.Utf8z(targetCanonicalId),
                    out IntPtr error);
                if (status != MecojoniNative.StatusOk)
                {
                    ThrowFrom(error, status);
                }
            }
            finally
            {
                GC.KeepAlive(this); // see MecoGrammar.EncodeArtifact
            }
        }

        /// <summary>Compiles the accumulated package rooted at
        /// `rootCanonicalId` (null or "" selects the first added module).
        /// The builder stays reusable afterwards.</summary>
        public MecoGrammar Compile(string rootCanonicalId = null)
        {
            try
            {
                int status = MecojoniNative.meco_package_builder_compile(
                    Handle, MecojoniNative.Utf8z(rootCanonicalId),
                    out IntPtr grammar, out IntPtr error);
                if (status != MecojoniNative.StatusOk)
                {
                    ThrowFrom(error, status);
                }
                return MecoGrammar.Adopt(grammar);
            }
            finally
            {
                GC.KeepAlive(this); // see MecoGrammar.EncodeArtifact
            }
        }
    }
}
