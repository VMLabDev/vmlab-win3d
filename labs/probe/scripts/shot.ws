// Screenshot through the wscript Machine handle (same daemon path as
// `vmlab vm screenshot`). Run with `vmlab script scripts/shot.ws`.
use vmlab

fn main(lab: Lab) {
    let Ok(m) = lab.vm("win11") else {
        lab.log("no vm win11")
        return
    }
    match m.screenshot("") {
        Ok(path) => lab.log("screenshot ok: " + path),
        Err(e) => lab.log("screenshot failed: " + e),
    }
    match m.ocr() {
        Ok(t) => lab.log("ocr ok: " + t),
        Err(e) => lab.log("ocr failed: " + e),
    }
}
