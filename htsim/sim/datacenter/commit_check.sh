# This script runs the validation script on a list of files and then runs the regression check script
# on the output files.

# The output directory where the output files will be stored. These files will be used by the regression
# check script to check for regressions compared to the baseline files in the `validate_outputs` directory.
validate_dir="validate_outputs"
old_validate_dir="validate_outputs_old"
branch_to_compare="origin/main"

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

# Fetch the latest changes from the remote repository
git fetch origin

# Remove the output directories if they exist and create new ones.
rm -rf "$validate_dir" "$old_validate_dir"
mkdir "$validate_dir" "$old_validate_dir"

# Loop through each file in the list
for file in "${files[@]}"; do
    echo "Running $file"

    # Create the output file name by replacing .txt with .out
    # Example: if file is "validate_uec_sender.txt" then
    # then output_filename is "validate_uec_sender.out"
    # and output_relative_dir will be "validate_outputs/validate_uec_sender.out"
    output_filename="${file%.txt}.out"
    output_relative_dir="$validate_dir/$output_filename"

    # Run the validation script and redirect output to the output file
    python3 validate.py $file >$output_relative_dir

    # Get the old output file from branch_to_compare and store it in the old_validate_dir
    git show $branch_to_compare:htsim/sim/datacenter/$output_relative_dir >$old_validate_dir/$output_filename

    # Run the regression check script on the output file
    # Example: if output_relative_dir is "validate_outputs/validate_uec_sender.out"
    #          then the command will be: python check_regressions.py validate_uec_sender.out --olddir "validate_outputs_old" --newdir "validate_outputs"
    python3 check_regressions.py $output_filename --olddir $old_validate_dir --newdir $validate_dir
done
