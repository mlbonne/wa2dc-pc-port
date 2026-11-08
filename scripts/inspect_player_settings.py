import sys

import UnityPy


def main(path):
    env = UnityPy.load(path)
    for obj in env.objects:
        if obj.type.name != "PlayerSettings":
            continue
        data = obj.read()
        for name in (
            "resizableWindow",
            "defaultScreenWidth",
            "defaultScreenHeight",
            "defaultIsFullScreen",
            "runInBackground",
            "companyName",
            "productName",
            "fullscreenMode",
            "d3d11FullscreenMode",
        ):
            try:
                print(name, "=", repr(getattr(data, name)))
            except Exception as exception:
                print(name, "!=", exception)
        print("typed attrs:", sorted(k for k in data.__dict__ if not k.startswith("_")))


if __name__ == "__main__":
    main(sys.argv[1])
