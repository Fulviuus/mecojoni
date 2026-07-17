// SPDX-License-Identifier: MPL-2.0
//
// Raw P/Invoke surface for the Mecojoni C ABI. The authoritative contract is
// engines/godot/mecojoni-ffi/include/mecojoni.h; every declaration here must
// match it exactly (struct layout, integer widths, pointer slots).
//
// Marshalling policy, chosen for portability across Unity's Mono and IL2CPP:
//  - Opaque objects are plain IntPtr handles; ownership is managed by the
//    public wrapper classes in Mecojoni.cs.
//  - `const char *` parameters are passed as NUL-terminated UTF-8 byte
//    arrays (the runtime pins byte[] for the call; the ABI copies before
//    returning). A null array marshals to NULL.
//  - MecoStrView results are copied out with Marshal.Copy + Encoding.UTF8
//    instead of relying on any ANSI/UTF-8 marshaller behaviour.
//  - MecoStrView is a 16-byte blittable struct returned by value, which both
//    Mono and IL2CPP marshal per the platform ABI.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Mecojoni
{
    internal static class MecojoniNative
    {
        // Resolves libmecojoni_ffi.dylib / libmecojoni_ffi.so /
        // mecojoni_ffi.dll from the Unity plugin folders.
        internal const string Lib = "mecojoni_ffi";

        internal const int StatusOk = 0;
        internal const int StatusError = 1;
        internal const int StatusInvalidArgument = 2;

        internal const uint ValueText = 0;
        internal const uint ValueNumber = 1;
        internal const uint ValueBoolean = 2;
        internal const uint ValueEnum = 3;

        /* Mirrors `MecoStrView`: { const char *data; size_t len; }. */
        [StructLayout(LayoutKind.Sequential)]
        internal struct MecoStrView
        {
            public IntPtr Data;
            public UIntPtr Len;
        }

        /* Mirrors `MecoDataBinding`: pointer, pointer, i64, u64, u32, u8. */
        [StructLayout(LayoutKind.Sequential)]
        internal struct MecoDataBinding
        {
            public IntPtr Name;
            public IntPtr Text;
            public long Numerator;
            public ulong Denominator;
            public uint Kind;
            public byte Boolean;
        }

        /* Mirrors `MecoGenerateOptions`. */
        [StructLayout(LayoutKind.Sequential)]
        internal struct MecoGenerateOptions
        {
            public IntPtr Entry;
            public IntPtr Data;
            public UIntPtr DataCount;
            public ulong Seed;
            public uint MaxDepth;
            public uint MaxExpansions;
            public uint MaxOutputScalars;
            public uint MaxOutputBytes;
            public uint MaxSamplerWords;
        }

        /* ---- versioning ------------------------------------------------ */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern uint meco_ffi_abi_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern uint meco_core_api_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_bytecode_version();

        /* ---- errors ---------------------------------------------------- */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_error_code(IntPtr error);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_error_message(IntPtr error);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void meco_error_free(IntPtr error);

        /* ---- package building and compilation -------------------------- */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr meco_package_builder_new();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_package_builder_add_module(
            IntPtr builder, byte[] canonicalId, byte[] displayName,
            byte[] sourceUtf8, UIntPtr sourceLen, out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_package_builder_resolve_import(
            IntPtr builder, byte[] moduleCanonicalId, byte[] authoredPath,
            byte[] targetCanonicalId, out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_package_builder_compile(
            IntPtr builder, byte[] rootCanonicalId, out IntPtr outGrammar,
            out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void meco_package_builder_free(IntPtr builder);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_grammar_compile(
            byte[] name, byte[] sourceUtf8, UIntPtr sourceLen,
            out IntPtr outGrammar, out IntPtr outError);

        /* ---- artifacts (compiled .mecob bytecode) ----------------------- */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_grammar_decode(
            byte[] bytes, UIntPtr len, out IntPtr outGrammar, out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_grammar_encode(
            IntPtr grammar, uint debugProfile, out IntPtr outArtifact,
            out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr meco_artifact_data(IntPtr artifact);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr meco_artifact_len(IntPtr artifact);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void meco_artifact_free(IntPtr artifact);

        /* ---- grammar introspection -------------------------------------- */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr meco_grammar_entry_count(IntPtr grammar);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_grammar_entry(IntPtr grammar, UIntPtr index);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_grammar_default_entry(IntPtr grammar);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr meco_grammar_rule_count(IntPtr grammar);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr meco_grammar_production_count(IntPtr grammar);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern UIntPtr meco_grammar_warning_count(IntPtr grammar);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_grammar_warning_code(IntPtr grammar, UIntPtr index);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_grammar_warning_message(IntPtr grammar, UIntPtr index);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void meco_grammar_free(IntPtr grammar);

        /* ---- generation -------------------------------------------------- */

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int meco_grammar_generate(
            IntPtr grammar, ref MecoGenerateOptions options,
            out IntPtr outGenerated, out IntPtr outError);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_generated_text(IntPtr generated);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern MecoStrView meco_generated_entry(IntPtr generated);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern uint meco_generated_expansions(IntPtr generated);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern uint meco_generated_sampler_words(IntPtr generated);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void meco_generated_free(IntPtr generated);

        /* ---- helpers ----------------------------------------------------- */

        /* UTF-8 with a trailing NUL, as the ABI's `const char *` expects.
         * Null input maps to a NULL pointer at the boundary. */
        internal static byte[] Utf8z(string value)
        {
            if (value == null)
            {
                return null;
            }
            int byteCount = Encoding.UTF8.GetByteCount(value);
            byte[] bytes = new byte[byteCount + 1];
            Encoding.UTF8.GetBytes(value, 0, value.Length, bytes, 0);
            bytes[byteCount] = 0;
            return bytes;
        }

        /* Copies a borrowed view into a managed string; absent views become
         * null. Must be called while the owning object is still alive. */
        internal static string ViewToString(MecoStrView view)
        {
            if (view.Data == IntPtr.Zero)
            {
                return null;
            }
            int length = checked((int)view.Len.ToUInt64());
            if (length == 0)
            {
                return string.Empty;
            }
            byte[] bytes = new byte[length];
            Marshal.Copy(view.Data, bytes, 0, length);
            return Encoding.UTF8.GetString(bytes);
        }

        /* Reads code and message out of a MecoError (which may be NULL when
         * the callee reported MECO_STATUS_INVALID_ARGUMENT) and frees it. */
        internal static void ConsumeError(
            IntPtr error, string fallbackCode, out string code, out string message)
        {
            if (error == IntPtr.Zero)
            {
                code = fallbackCode;
                message = "mecojoni call failed without diagnostics";
                return;
            }
            code = ViewToString(meco_error_code(error)) ?? fallbackCode;
            message = ViewToString(meco_error_message(error)) ?? "";
            meco_error_free(error);
        }
    }
}
