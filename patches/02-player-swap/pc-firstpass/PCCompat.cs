using UnityEngine;

// PC port shim: Unity 3.5's Windows UnityEngine.dll does not contain the
// iOS-style TouchScreenKeyboard API. EZGui references it for on-screen
// keyboards; on PC UIManager's keyboard path is not needed (Application.isEditor
// is false, but our stub simply returns an inert object). Defining the types in
// this assembly makes them resolvable for the first-pass build without touching
// the player's UnityEngine.dll.
namespace UnityEngine
{
    public enum TouchScreenKeyboardType
    {
        Default = 0,
        ASCIICapable = 1,
        NumbersAndPunctuation = 2,
        URL = 3,
        NumberPad = 4,
        PhonePad = 5,
        NamePhonePad = 6,
        EmailAddress = 7,
    }

    public class TouchScreenKeyboard
    {
        public static bool hideInput;
        public static bool autorotateToPortrait;
        public static bool autorotateToPortraitUpsideDown;
        public static bool autorotateToLandscapeLeft;
        public static bool autorotateToLandscapeRight;

        public string text = string.Empty;
        public bool active;
        public bool done = true;

        public static TouchScreenKeyboard Open(string text, TouchScreenKeyboardType type, bool autocorrect, bool multiline, bool secure, bool alert, string placeholder)
        {
            return new TouchScreenKeyboard { text = text, active = false, done = true };
        }
    }
}
