import unittest

from scripts.maomi_simulator import Simulator


class SimulatorTest(unittest.TestCase):
    def setUp(self):
        self.sim = Simulator()
        self.addCleanup(self.sim.close)

    def test_batches_follow_book_order_without_filling_with_completed_words(self):
        self.sim.call('adopt', 'Test')
        self.sim.import_words([(w, w) for w in ('a', 'b', 'c', 'd', 'e', 'f', 'g')])
        state = self.sim.call('start', 'en_zh')
        self.assertEqual(state['target'], 5)
        for word in ('a', 'b', 'c', 'd', 'e'):
            self.assertEqual(state['word'], word)
            state = self.sim.call('answer', 'correct')
        state = self.sim.call('start', 'en_zh')
        self.assertEqual(state['target'], 2)
        for word in ('f', 'g'):
            self.assertEqual(state['word'], word)
            state = self.sim.call('answer', 'correct')
        self.assertEqual(self.sim.call('start', 'en_zh')['error'], 'no_words_due')
        self.assertFalse(self.sim.call('status')['active'])

    def test_three_spaced_successes_master_and_survive_restart_and_import(self):
        self.sim.call('adopt', 'Test')
        book = [('cat', '猫')]
        self.sim.import_words(book)
        for hours in (0, 24, 72):
            if hours:
                self.sim.call('advance', hours - 1)
                self.assertEqual(self.sim.call('start', 'en_zh')['error'], 'no_words_due')
                self.sim.call('advance', 1)
            self.assertTrue(self.sim.call('start', 'en_zh')['active'])
            state = self.sim.call('answer', 'correct')
        self.assertEqual((state['unlearned_count'], state['consolidating_count'], state['mastered_count']), (0, 0, 1))
        self.sim.call('advance', 24 * 90)
        self.sim.call('restart')
        self.sim.import_words(book)
        self.assertEqual(self.sim.call('start', 'en_zh')['error'], 'all_mastered')
        # Changing meaning reopens learning without discarding the pet or wallet.
        coins = self.sim.call('status')['coins']
        state = self.sim.import_words([('cat', '猫咪')])
        self.assertEqual(state['unlearned_count'], 1)
        self.assertEqual(state['coins'], coins)
        self.assertTrue(self.sim.call('start', 'en_zh')['active'])

    def test_wrong_and_hinted_reset_streak_and_due_words_precede_new_words(self):
        for verdict in ('wrong', 'hinted'):
            with self.subTest(verdict=verdict):
                self.sim.call('reset')
                self.sim.call('adopt', 'Test')
                self.sim.import_words([('cat', '猫')])
                self.sim.call('start', 'en_zh')
                self.sim.call('answer', 'correct')
                self.sim.call('advance', 24)
                self.sim.call('start', 'en_zh')
                state = self.sim.call('answer', verdict)
                if verdict == 'wrong':
                    self.assertTrue(state['correction'])
                    self.sim.call('answer', 'corrected')
                self.sim.import_words([('dog', '狗'), ('cat', '猫')])
                self.sim.call('advance', 24)
                state = self.sim.call('start', 'en_zh')
                self.assertEqual(state['word'], 'cat')
                self.assertEqual(state['due_count'], 1)
                self.sim.call('answer', 'correct')
                self.sim.call('answer', 'correct')
                self.sim.call('advance', 24)
                self.sim.call('start', 'en_zh')
                self.sim.call('answer', 'correct')
                state = self.sim.call('answer', 'correct')
                self.assertEqual(state['mastered_count'], 0)
                self.sim.call('advance', 72)
                self.sim.call('start', 'en_zh')
                self.sim.call('answer', 'correct')
                self.assertEqual(self.sim.call('answer', 'correct')['mastered_count'], 2)

    def test_mastered_review_is_explicit_and_wrong_answers_reopen_learning(self):
        self.sim.call('adopt', 'Test')
        self.sim.import_words([('cat', '猫')])
        self.assertEqual(self.sim.call('start', 'en_zh', 'review_mastered')['error'], 'no_mastered_words')
        for hours in (0, 24, 72):
            self.sim.call('advance', hours)
            self.sim.call('start', 'en_zh')
            self.sim.call('answer', 'correct')
        state = self.sim.call('start', 'en_zh', 'review_mastered')
        self.assertTrue(state['active'])
        self.assertEqual(self.sim.call('answer', 'correct')['mastered_count'], 1)
        self.sim.call('start', 'en_zh', 'review_mastered')
        self.sim.call('answer', 'wrong')
        state = self.sim.call('answer', 'corrected')
        self.assertEqual((state['mastered_count'], state['consolidating_count']), (0, 1))
        self.sim.call('advance', 24)
        self.assertEqual(self.sim.call('start', 'en_zh')['word'], 'cat')

    def test_real_engine_learning_care_and_restart(self):
        state = self.sim.call('adopt', '小橘')
        self.assertEqual(state['name'], '小橘')
        self.assertEqual(state['age_days'], 0)
        self.sim.import_words([('cat', '猫'), ('dog', '狗'), ('apple', '苹果'),
                               ('book', '书'), ('sun', '太阳')])
        self.sim.call('start', 'en_zh')
        for _ in range(5):
            state = self.sim.call('answer', 'correct')
        self.assertEqual(state['coins'], 20)
        self.assertFalse(state['active'])
        self.assertEqual(self.sim.call('buy', 'food', 2)['coins'], 12)
        self.sim.call('advance', 24)
        self.sim.call('care', 'feed')
        self.sim.call('care', 'feed')
        state = self.sim.call('care', 'clean')
        self.assertEqual((state['age_days'], state['care_days'], state['poop']), (1, 1, 0))
        restored = self.sim.call('restart')
        for key in ('name', 'coins', 'food', 'litter', 'care_days', 'age_days'):
            self.assertEqual(restored[key], state[key])

    def test_failed_purchase_and_corrections_do_not_create_coins(self):
        self.sim.call('adopt', 'Test')
        state = self.sim.call('buy', 'food', 1)
        self.assertFalse(state['ok'])
        self.assertEqual(state['error'], 'insufficient_coins')
        self.sim.import_words([('cat', '猫')])
        self.sim.call('start', 'zh_en')
        state = self.sim.call('answer', 'wrong')
        self.assertTrue(state['correction'])
        self.assertEqual(state['coins'], 0)
        state = self.sim.call('answer', 'corrected')
        self.assertEqual(state['coins'], 10)
        self.assertFalse(state['active'])
        self.assertEqual(self.sim.call('start', 'zh_en')['error'], 'no_words_due')
        self.assertEqual(self.sim.call('answer', 'correct')['error'], 'stale_question')
        self.assertEqual(self.sim.call('status')['coins'], 10)
        self.sim.call('advance', 24)
        self.sim.call('start', 'zh_en')
        self.assertEqual(self.sim.call('answer', 'correct')['coins'], 22)

    def test_empty_name_rejected_and_unicode_name_round_trips(self):
        self.assertEqual(self.sim.call('adopt', '')['error'], 'invalid_name')
        name = '小"橘\\'
        self.assertEqual(self.sim.call('adopt', name)['name'], name)
        self.assertEqual(self.sim.call('restart')['name'], name)

    def test_questions_do_not_reveal_answers_in_either_direction(self):
        for mode in ('en_zh', 'zh_en'):
            with self.subTest(mode=mode):
                self.sim.call('reset')
                self.sim.call('adopt', 'Test')
                self.sim.import_words([('cat', '猫'), ('dog', '狗')])
                state = self.sim.call('start', mode)

                def check_question(current):
                    shown = current['word'] if mode == 'en_zh' else current['meaning']
                    hidden = current['meaning'] if mode == 'en_zh' else current['word']
                    question = '这个是什么意思？' if mode == 'en_zh' else '这个用英语怎么说？'
                    self.assertEqual(current['question_prompt'], f'{shown}，{question}')
                    self.assertNotIn(hidden, current['question_prompt'])

                check_question(state)
                # Resuming or asking again must not switch to demonstrating the answer.
                check_question(self.sim.call('restart'))
                repeated = self.sim.call('answer', 'unclear')
                self.assertEqual(repeated['question'], state['question'])
                check_question(repeated)
                check_question(self.sim.call('answer', 'wrong'))
                correction = self.sim.call('answer', 'wrong')
                self.assertTrue(correction['correction'])
                check_question(correction)
                check_question(self.sim.call('answer', 'corrected'))
                finished = self.sim.call('answer', 'corrected')
                self.assertFalse(finished['active'])
                self.assertNotIn('question_prompt', finished)
                # Due review words use the same question-first flow as new words.
                self.sim.call('advance', 24)
                check_question(self.sim.call('start', mode))
                self.assertNotIn('question_prompt', self.sim.call('stop'))
