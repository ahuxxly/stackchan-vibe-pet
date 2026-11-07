"""Real main.cpp page/input/motion/HUD functions + real LocalAudio worker.

Only hardware/host output and affection display values are fake. The real
gesture/affection/NVS suite separately covers storage and rewards.
"""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def render(root,scenarios):
    source=(root/'src/main.cpp').read_text()
    def body(signature):
        at=source.index(signature);end=source.index('{',at)+1;depth=1
        while depth:
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        return source[at:end].replace('millis()','appNow()')
    globals_=source[source.index('Page page='):source.index('void applyAffection(')]
    signatures=('void applyAffection(', 'bool selectedFresh(', 'bool selectedWaiting(', 'bool selectedApproval(',
        'uint32_t hostColor(', 'int requestServoPower(', 'bool powerOffContext()', 'void updatePowerDiagnostics()', 'void motionOff(', 'void failMotion(', 'void failWarmup(', 'void failImu(',
        'void switchPage(', 'void setScreen(', 'void execute(', 'void updateInput(',
        'PetHudSnapshot capturePetHud(', 'void updateMotion(', 'void updateImu(')
    functions=[body(sig) for sig in signatures if sig in source]
    return '#include "app_integration_support.h"\n'+globals_+'\n'+'\n'.join(functions)+'\n'+scenarios

if __name__=='__main__':
    output=ROOT/'.test-bin/app_integration_test.cpp'
    output.write_text(render(ROOT,(ROOT/'tests/app_integration_scenarios.cpp').read_text()))
    binary=output.with_suffix('')
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-Itests','-Itests/stubs/audio','-Isrc',str(output),'src/LocalAudio.cpp','-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=45)
