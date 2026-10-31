"""Test actual preview request logic without importing serial or opening hardware."""

import ast
from pathlib import Path
import threading
import unittest
from unittest.mock import Mock


class PreviewDetectionTest(unittest.TestCase):
    def setUp(self):
        source = Path(__file__).with_name('reliable_preview.py')
        tree = ast.parse(source.read_text(encoding='utf-8'), filename=str(source))
        # Load only these existing function bodies: the viewer's module-level
        # UART setup and server entry point must not execute during tests.
        functions = [node for node in tree.body
                     if isinstance(node, ast.FunctionDef)
                     and node.name in ('describe', 'run_detection')]
        self.assertEqual(len(functions), 2)
        self.state = {'pose_busy': True}
        self.pose_once = Mock()
        self.stop = Mock()
        namespace = dict(lock=threading.Lock(), state=self.state,
                         pose_once=self.pose_once, stop=self.stop)
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), 'exec'), namespace)
        self.run_detection = namespace['run_detection']

    @staticmethod
    def sample(count, **overrides):
        result = dict(ret='ESP_OK', valid='1', cal_count=str(count),
                      calibrated='1' if count == 3 else '0', status='1')
        result.update(overrides)
        return result

    def test_calibration_resets_only_first_frame(self):
        self.pose_once.side_effect = [self.sample(i) for i in (1, 2, 3)]
        self.run_detection('calibrate', 90)
        self.assertEqual([call.args for call in self.pose_once.call_args_list],
                         [('calibrate', 90), ('pose', 90), ('pose', 90)])
        self.assertIn('坐正校准完成', self.state['pose_result'])
        self.assertFalse(self.state['pose_busy'])

    def test_each_new_calibration_restarts_collection(self):
        self.pose_once.side_effect = [self.sample(i) for i in (1, 2, 3, 1, 2, 3)]
        self.run_detection('calibrate', 0)
        self.run_detection('calibrate', 180)
        self.assertEqual([call.args[0] for call in self.pose_once.call_args_list],
                         ['calibrate', 'pose', 'pose', 'calibrate', 'pose', 'pose'])

    def test_failed_frame_does_not_report_calibration_success(self):
        self.pose_once.side_effect = [self.sample(1),
                                      self.sample(1, ret='ESP_ERR_TIMEOUT', valid='0')]
        self.run_detection('calibrate', 270)
        self.assertEqual(self.pose_once.call_count, 2)
        self.assertIn('校准未完成', self.state['pose_result'])
        self.assertFalse(self.state['pose_busy'])

    def test_unstable_samples_do_not_report_success(self):
        self.pose_once.side_effect = [self.sample(1) for _ in range(3)]
        self.run_detection('calibrate', 0)
        self.assertIn('1/3', self.state['pose_result'])
        self.assertNotIn('坐正校准完成', self.state['pose_result'])

    def test_single_pose_does_not_reset_calibration(self):
        self.pose_once.return_value = self.sample(3, elapsed_ms='1200')
        self.run_detection('pose', 90)
        self.pose_once.assert_called_once_with('pose', 90)
        self.assertIn('1.20秒', self.state['pose_result'])
        self.assertFalse(self.state['pose_busy'])


if __name__ == '__main__':
    unittest.main()
