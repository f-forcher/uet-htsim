rm -rf test_outputs
mkdir test_outputs
echo "Running validate_uec_sender.txt"
python validate.py validate_uec_sender.txt > test_outputs/validate_uec_sender.out
python check_regressions.py validate_uec_sender.out

echo "Running validate_uec_rcv.txt"
python validate.py validate_uec_rcv.txt > test_outputs/validate_uec_rcv.out
python check_regressions.py validate_uec_rcv.out

echo "Running validate_uec_both.txt"
python validate.py validate_uec_both.txt > test_outputs/validate_uec_both.out
python check_regressions.py validate_uec_both.out

echo "Running validate_load_balancing_snd.txt"
python validate.py validate_load_balancing_snd.txt > test_outputs/validate_load_balancing_snd.out
python check_regressions.py validate_load_balancing_snd.out

echo "Running validate_load_balancing_rcv.txt"
python validate.py validate_load_balancing_rcv.txt > test_outputs/validate_load_balancing_rcv.out
python check_regressions.py validate_load_balancing_rcv.out

echo "Running validate_load_balancing_failed_snd.txt"
python validate.py validate_load_balancing_failed_snd.txt > test_outputs/validate_load_balancing_failed_snd.out
python check_regressions.py validate_load_balancing_failed_snd.out

echo "Running validate_load_balancing_failed_rcv.txt"
python validate.py validate_load_balancing_failed_rcv.txt > test_outputs/validate_load_balancing_failed_rcv.out
python check_regressions.py validate_load_balancing_failed_rcv.out
