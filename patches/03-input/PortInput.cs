using System.IO;
using UnityEngine;

// PC input extras for the PVSelect dress-up screen. The arrows are game
// TouchSprites and already receive the mouse through BaseScene.CheckTouch();
// these keyboard bindings remain only as a convenience.
//
// Drop port-shot.request next to the executable and the game writes a
// render-resolution screenshot (used by the fullscreen playtest, where a GDI
// capture cannot see the game).
public class PortInput : MonoBehaviour
{
    public static void Ensure()
    {
        if (GameObject.Find("Port Input") != null)
        {
            return;
        }
        GameObject gameObject = new GameObject("Port Input");
        UnityEngine.Object.DontDestroyOnLoad(gameObject);
        gameObject.AddComponent<PortInput>();
    }

    private void Update()
    {
        PortCurtainSkin.Tick();

        string request = Application.dataPath + "/../port-shot.request";
        if (File.Exists(request))
        {
            try
            {
                File.Delete(request);
                Application.CaptureScreenshot(Application.dataPath + "/../port-shot.png");
                Debug.Log("PortInput: screenshot written to port-shot.png");
            }
            catch (IOException)
            {
                // The test harness may be rewriting the request; retry next frame.
            }
        }

        PVSelectController controller = UnityEngine.Object.FindObjectOfType(typeof(PVSelectController)) as PVSelectController;
        if (controller == null)
        {
            return;
        }
        if (Input.GetKeyDown(KeyCode.RightArrow))
        {
            controller.NextItem();
        }
        if (Input.GetKeyDown(KeyCode.LeftArrow))
        {
            controller.PreItem();
        }
    }
}
