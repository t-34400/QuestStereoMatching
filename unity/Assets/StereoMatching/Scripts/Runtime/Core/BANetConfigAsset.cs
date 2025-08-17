# nullable enable

using System.IO;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Networking;

namespace StereoMatching
{
    [CreateAssetMenu(menuName = "StereoMatching/Config/BANet", fileName = "BANetConfigAsset")]
    sealed class BANetConfigAsset : ScriptableObject
    {
        [SerializeField] string modelFileName = "banet.pte";
        [SerializeField] Vector2Int inputSize = new Vector2Int(640, 480);

        public async Task<Native.BANet_Config> ToNativeConfigAsync()
        {
            var dst = await CopyModelToPersistentDataAsyncIfNeeded(modelFileName);
            return new Native.BANet_Config
            {
                modelPath = dst,
                inputWidth = inputSize.x,
                inputHeight = inputSize.y,
            };
        }

        private async Task<string> CopyModelToPersistentDataAsyncIfNeeded(string modelFileName)
        {
            var src = Path.Combine(Application.streamingAssetsPath, modelFileName);
            var dstDir = Application.persistentDataPath;
            var dst = Path.Combine(dstDir, modelFileName);

            Directory.CreateDirectory(dstDir);

            if (!File.Exists(dst))
            {
                var data = await ReadStreamingAsset(src);
                if (data == null || data.Length == 0)
                    throw new IOException("Failed to read model from StreamingAssets.");
                File.WriteAllBytes(dst, data);
            }

            if (dst.Length >= 256)
                throw new IOException("Model path is too long for BANet_Config (>=256).");

            return dst;
        }

        static async Task<byte[]> ReadStreamingAsset(string path)
        {
            if (path.StartsWith("jar:") || path.StartsWith("http") || Application.platform == RuntimePlatform.Android)
            {
                using (var req = UnityWebRequest.Get(path))
                {
                    var op = req.SendWebRequest();
#if UNITY_2020_2_OR_NEWER
                    while (!op.isDone) await Task.Yield();
                    if (req.result != UnityWebRequest.Result.Success) throw new IOException($"UnityWebRequest error: {req.error}");
#else
                    while (!op.isDone) await Task.Yield();
                    if (req.isNetworkError || req.isHttpError) throw new IOException($"UnityWebRequest error: {req.error}");
#endif
                    return req.downloadHandler.data;
                }
            }
            if (!File.Exists(path)) throw new FileNotFoundException("StreamingAssets file not found.", path);
            return File.ReadAllBytes(path);
        }
    }
}