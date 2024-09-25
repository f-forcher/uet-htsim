# This script runs the validation script on a list of files and then runs the regression check script
# on the output files.

# The output directory where the output files will be stored. These files will be used by the regression
# check script to check for regressions compared to the baseline files in the `validate_outputs` directory.
validate_dir="validate_outputs"

# Remove the output directory if it exists and create a new one
rm -rf "$validate_dir"
mkdir "$validate_dir"

# List of files to be processed
files=(
    "validate_uec_sender.txt"
    "validate_uec_rcv.txt"
    "validate_uec_both.txt"
    "validate_load_balancing_snd.txt"
    "validate_load_balancing_rcv.txt"
    "validate_load_balancing_failed_snd.txt"
    "validate_load_balancing_failed_rcv.txt"
)

# Loop through each file in the list
for file in "${files[@]}"; do
    echo "Running $file"

    # Create the output file name by replacing .txt with .out
    # Example: if file is "validate_uec_sender.txt" then output_file will be "test_outputs/validate_uec_sender.out"
    output_file="$validate_dir/${file%.txt}.out"

    # Run the validation script and redirect output to the output file
    python3 validate.py "$file" >"$output_file"

    # Run the regression check script on the output file
    # Example: if output_file is "test_outputs/validate_uec_sender.out"
    #          then the command will be: python check_regressions.py validate_uec_sender.out
    python3 check_regressions.py "$(basename "$output_file")"
done
