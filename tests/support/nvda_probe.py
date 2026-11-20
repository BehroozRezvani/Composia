"""Loaded only in the isolated NVDA profile created by accessibility_tools.py."""
import json
import os
from pathlib import Path
import traceback

import core
import globalPluginHandler
import speech
import textInfos
import UIAHandler
from NVDAObjects.UIA import UIA


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
    def __init__(self):
        super().__init__()
        core.callLater(500, self.inspect)

    def inspect(self):
        result = Path(os.environ["COMPOSIA_NVDA_RESULT"])
        try:
            handler = UIAHandler.handler
            hwnd = int(os.environ["COMPOSIA_TEST_HWND"])
            pane = handler.clientObject.ElementFromHandle(hwnd)
            output = {}
            for identifier in ("volume", "devices", "device1", "apply"):
                condition = handler.clientObject.CreatePropertyCondition(
                    UIAHandler.UIA_AutomationIdPropertyId, identifier)
                element = pane.FindFirstBuildCache(
                    UIAHandler.TreeScope_Descendants, condition, handler.baseCacheRequest)
                obj = UIA(windowHandle=hwnd, UIAElement=element)
                words = [s for s in speech.getObjectSpeech(obj) if isinstance(s, str)]
                output[identifier] = {"name": obj.name, "role": obj.role.name,
                                      "value": obj.value, "speech": " ".join(words)}
            notes = pane.FindFirstBuildCache(
                UIAHandler.TreeScope_Descendants,
                handler.clientObject.CreatePropertyCondition(UIAHandler.UIA_NamePropertyId, "Notes"),
                handler.baseCacheRequest)
            editor = UIA(windowHandle=hwnd, UIAElement=notes)
            output["document"] = editor.makeTextInfo(textInfos.POSITION_ALL).text
        except Exception:
            output = {"error": traceback.format_exc()}
        tmp = result.with_suffix(".tmp")
        tmp.write_text(json.dumps(output), encoding="utf-8")
        tmp.replace(result)
        core.callLater(100, core.triggerNVDAExit)
