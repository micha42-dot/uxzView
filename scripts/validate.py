"""Run reproducible local checks; native Windows checks require an interactive desktop.

No build, downloads, deployment, publishing or source changes are performed.
Reports go into the selected build directory. Run UI suites sequentially.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
NATIVE_GROUPS = {
    'native-window-test-results': '''fullscreenFitsWholeImageWithoutFrame
        saveDialogPausesSlideshowAndRetainsChosenImage exportedAnimationBecomesSinglePageDocument
        fileOpenDuringSaveIsDeferredUntilTransactionEnds keyboardFolderNavigation
        duplicateResultOpensWithoutManualDialogClosing fullscreenCloseKeepsWindowFitPreference
        saveReadOnlyFormatOffersSaveAs uppercaseJpegSaveRetainsLosslessCommands watermarkConvertsTaggedColorSpace
        fittedImageHasNoPhantomScrollbarsAndWheelNavigates imageChangesDiscardEditsByDefaultButClosingStillAsks
        imageChangeSaveQuestionCanBeEnabled aspectRatioCropMenuAppliesAndUndoes fullscreenMouseNavigationAndDrags
        neighborPreviewsReuseWidgetsAndRefreshChangedFiles'''.split(),
    'native-toolbar-test-results': '''optionalWidgetsAndNeighborNavigation outlineIconsAndZoomSemantics
        selectionDamageCoversEveryChangedPixel metadataStreamsUnicodeReadOnlyFiles grayscale16RetainsEverySampleAcrossEditing
        editedAnimationResumeHonorsSavePolicy activeDocumentReloadsAfterFileUndoAndPipeline
        dirtyDocumentSurvivesPipelineReplacementAndFileUndo undoRedoDisplayRunsInWorker apngSessionsAdvanceLinearlyAndInvalidate
        toolbarStatesStayStableWhileBrowsing settingsCategoriesRemainVisible previewBackgroundModesPreserveThumbnails
        settingsRoundTripAndCancel portableModeSettingsPreserveCancelAndImport fullscreenTransitionsRetainVisibleWindow
        fullscreenInformationOptionsAndMenu fullscreenInformationTracksImagesWithoutChangingPixels'''.split(),
    'native-review-test-results': '''displayCachePreservesPixelsAndInvalidates cacheRejectsFilesChangedDuringDecoding
        cacheInvalidatesSameSizeRewriteWithPreservedMtime navigationSurvivesAnimationPageLoading psd'''.split(),
    'native-fullscreen-results': '''fittedImageHasNoPhantomScrollbarsAndWheelNavigates fullscreenTransitionsRetainVisibleWindow
        fullscreenFitsWholeImageWithoutFrame fullscreenCloseKeepsWindowFitPreference fullscreenMouseNavigationAndDrags
        keyboardZoomAndEditingFocus'''.split(),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--suite', choices=('core', 'native', 'all'), default='core')
    parser.add_argument('--require-assets', action='store_true', help='Fail rather than skip missing RAW/tool fixtures')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not (build / 'CMakeCache.txt').is_file():
        parser.error('Configure and build the selected directory first')
    if args.suite != 'core' and sys.platform != 'win32':
        parser.error('The native suite currently targets the Windows desktop')
    env = os.environ.copy()
    sdk = ROOT / '.deps/sdk/ucrt64/bin'
    if sys.platform == 'win32' and sdk.is_dir():
        env['PATH'] = str(sdk) + os.pathsep + env.get('PATH', '')
    # Make the main native run independent of a previous scaled/offscreen shell.
    for key in ('QT_QPA_PLATFORM', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'IX_PROFILE_TRANSITIONS'):
        env.pop(key, None)
    if args.require_assets:
        env['IX_REQUIRE_RAW'] = env['IX_REQUIRE_TOOLS'] = '1'
    report = {'started_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'python': platform.python_version(), 'build': str(build), 'suite': args.suite,
              'require_assets': args.require_assets, 'steps': [], 'success': False}
    summary = build / 'validation-summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, timeout=300, extra=None):
        log = build / (name + '.log')
        step = {'name': name, 'command': [str(x) for x in command], 'log': log.name}
        started = time.monotonic()
        print(f'Running {name}', flush=True)
        try:
            with log.open('w', encoding='utf-8') as output:
                result = subprocess.run(step['command'], cwd=ROOT, env=env | (extra or {}),
                    stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
            step['returncode'] = result.returncode
            if result.returncode:
                raise RuntimeError(f'{name} failed; see {log}')
        except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
            step['error'] = str(error)
            raise
        finally:
            step['seconds'] = round(time.monotonic() - started, 3)
            report['steps'].append(step)
            summary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def native(name, cases, scale=None, engine='ixview_tests'):
        command = [build / (engine + '.exe'), *cases, '-platform', 'windows',
                   '-o', str(build / (name + '.txt')) + ',txt',
                   '-o', str(build / (name + '.xml')) + ',junitxml']
        run(name, command, extra={'QT_SCALE_FACTOR': scale} if scale else None)

    try:
        if args.suite in ('core', 'all'):
            run('python-tests', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py', '-v'])
            ctest = shutil.which('ctest', path=env.get('PATH'))
            if not ctest: raise RuntimeError('CTest not found in PATH')
            run('ctest', [ctest, '--test-dir', build, '--output-on-failure', '-j', '1'], timeout=600)
        if args.suite in ('native', 'all'):
            for name, cases in NATIVE_GROUPS.items(): native(name, cases)
            native('native-workflow-test-results', [], engine='ixview_workflow_tests')
            native('native-extension-test-results', [], engine='ixview_extension_tests')
            for scale in ('1', '1.25', '1.5', '2'):
                native(f'native-review-display-scale-{scale}', ['displayCachePreservesPixelsAndInvalidates'], scale)
                native(f'native-fullscreen-scale-{scale}', ['fittedImageHasNoPhantomScrollbarsAndWheelNavigates',
                                                           'fullscreenTransitionsRetainVisibleWindow'], scale)
                native(f'native-clean-ui-scale-{scale}', ['customCropRatiosSettingsValidateAndPersist',
                                                        'settingsCategoriesRemainVisible'], scale)
                native(f'native-appearance-scale-{scale}', ['interfaceThemePreservesImageAndNavigationState',
                    'liveLanguageChangePreservesDocument:window',
                    'themedDialogsRemainUsable:light-de', 'themedDialogsRemainUsable:dark-de',
                    'themedDialogsRemainUsable:light-ko', 'themedDialogsRemainUsable:dark-ko',
                    'themedDialogsRemainUsable:light-fr', 'themedDialogsRemainUsable:dark-fr'],
                    scale, engine='ixview_workflow_tests')
        report['success'] = True
    except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
        report['error'] = str(error)
        print(error, file=sys.stderr)
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        summary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Report: {summary}', flush=True)
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
