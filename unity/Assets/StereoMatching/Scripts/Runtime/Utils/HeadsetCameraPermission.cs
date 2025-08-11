using UnityEngine;
using UnityEngine.Events;
using UnityEngine.Android;

namespace StereoMatching
{
    public class HeadsetCameraPermission : MonoBehaviour
    {
        [System.Serializable] public class PermissionGrantedEvent : UnityEvent { }

        public PermissionGrantedEvent onPermissionsGranted;

        const string HeadsetCam = "horizonos.permission.HEADSET_CAMERA";

        void Start()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            CheckAndRequest();
#endif
        }

        void OnApplicationFocus(bool hasFocus)
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            if (hasFocus && AllGranted()) onPermissionsGranted?.Invoke();
#endif
        }

        bool AllGranted()
        {
            return Permission.HasUserAuthorizedPermission(Permission.Camera) &&
                Permission.HasUserAuthorizedPermission(HeadsetCam);
        }

        void CheckAndRequest()
        {
            if (AllGranted()) { onPermissionsGranted?.Invoke(); return; }

            var list = new System.Collections.Generic.List<string>(2);
            if (!Permission.HasUserAuthorizedPermission(Permission.Camera)) list.Add(Permission.Camera);
            if (!Permission.HasUserAuthorizedPermission(HeadsetCam)) list.Add(HeadsetCam);
            if (list.Count > 0) Permission.RequestUserPermissions(list.ToArray());
        }
    }
}