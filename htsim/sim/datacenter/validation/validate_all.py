import json
import argparse
from itertools import product
import subprocess
import os
import shutil
import anlysis_and_plotting

# Some Global Variables
exp_directory = "experiments"

def delete_folder_contents(folder_path):
    for filename in os.listdir(folder_path):
        file_path = os.path.join(folder_path, filename)
        try:
            if os.path.isfile(file_path) or os.path.islink(file_path):
                os.unlink(file_path)  # Remove the file or link
            elif os.path.isdir(file_path):
                shutil.rmtree(file_path)  # Remove the directory and its contents
        except Exception as e:
            print(f'Failed to delete {file_path}. Reason: {e}')

def get_global_config(global_parameters):
    global_string = ""
    global_string += global_parameters["cc_algo"]
    global_string += f"_os_ratio{global_parameters['oversubscription_ratio']}"
    global_string += f"_size_topo{global_parameters['topology_sizes']}"
    global_string += f"_link_speed{global_parameters['link_speed_Gbps']}"
    return global_string

def get_cc_name(parameters_experiment):
    if (parameters_experiment["cc_algo"] == "receiver_based"):
        return ""
    elif (parameters_experiment["cc_algo"] == "sender_based"):
        return "-sender_cc_only"
    elif (parameters_experiment["cc_algo"] == "mixed"):
        return "-sender_cc"
    else:
        # Return error and exit the program
        print("Error: Invalid CC Algorithm")
        exit(1)

def get_topology_file(topology_size, os_ratio):
    os_ratio = os_ratio[0]  

    if (topology_size == 128):
        return f"../topologies/fat_tree_128_{os_ratio}os_100.topo"

def get_file_to_run(name_exp, parameters_experiment, global_params):
    dir = f"{name_exp}_size{global_params['topology_sizes']}_osratio{global_params['oversubscription_ratio']}_linkspeed{global_params['link_speed_Gbps']}"
    if (name_exp == "incast"):
        cm_name = f"{exp_directory}/{dir}/incast_{parameters_experiment['ratio']}to1_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = (f"{exp_directory}/{dir}/incast_{parameters_experiment['ratio']}to1_size{parameters_experiment['message_size_bytes']}B_")
        cmd_to_run_cm_file = "python ../connection_matrices/gen_incast.py {} 128 {} {} 0 42 1".format(cm_name, parameters_experiment["ratio"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            """ print(f"Creating CM named {cmd_to_run_cm_file}") """
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "permutation"):
        cm_name = f"{exp_directory}/{dir}/permutation_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = f"{exp_directory}/{dir}/permutation_size{parameters_experiment['message_size_bytes']}B_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_permutation.py {} 128 {} {} 0 42".format(cm_name, 128, parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            """ print(f"Creating CM named {cmd_to_run_cm_file}") """
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file


def read_json_file(file_path):
    with open(file_path, 'r') as file:
        data = json.load(file)
    return data

def get_global_combinations(global_parameters):
    keys = global_parameters.keys()
    values = (global_parameters[key] if isinstance(global_parameters[key], list) else [global_parameters[key]] for key in keys)
    return [dict(zip(keys, combination)) for combination in product(*values)]

def run_experiment(experiment_name, global_params, subparams):
    # Dummy line to simulate running the experiment
    print(f"Running {experiment_name} with global parameters: {global_params} and subparameters: {subparams}")

    connection_matrix, output_file = get_file_to_run(experiment_name, subparams, global_params)

    output_file =  output_file + get_global_config(global_params) + ".out"
    topo_file = get_topology_file(global_params["topology_sizes"], global_params["oversubscription_ratio"])
    # Define the static command to execute
    cc_algo_to_use = get_cc_name(global_params)
    command = "../htsim_uec -tm {} -end 1000000 {} -topo {} > {}".format(connection_matrix, cc_algo_to_use, topo_file, output_file)
    print(f"Executing: {command}")
    try:
        # Execute the command
        subprocess.run(command, shell=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while running the command: {e}")

def handle_experiment(experiment, global_combinations, global_params):
    """ for global_params in global_combinations:
        print("GLOBAL")
        print(global_params)
        subparam_keys = [key for key in experiment.keys() if key != 'name']
        subparam_values = (experiment[key] if isinstance(experiment[key], list) else [experiment[key]] for key in subparam_keys)
        directory = os.path.join(exp_directory, f"{experiment['name']}_size{global_params['topology_sizes']}_osratio{global_params['oversubscription_ratio']}_linkspeed{global_params['link_speed_Gbps']}")
        if not os.path.exists(directory):
            os.makedirs(directory)
        delete_folder_contents(directory)
        for subparam_combination in product(*subparam_values):
            subparams = dict(zip(subparam_keys, subparam_combination))
            run_experiment(experiment['name'], global_params, subparams)
        anlysis_and_plotting.plot_runtimes(directory) """

    for link_speed in global_params["link_speed_Gbps"]:
        for os_ratio in global_params["oversubscription_ratio"]:
            for topology_size in global_params["topology_sizes"]:
                directory = os.path.join(exp_directory, f"{experiment['name']}_size{topology_size}_osratio{os_ratio}_linkspeed{link_speed}")
                if not os.path.exists(directory):
                    os.makedirs(directory)
                delete_folder_contents(directory)
                for cc_algo in global_params["cc_algo"]:
                    subparam_keys = [key for key in experiment.keys() if key != 'name']
                    subparam_values = (experiment[key] if isinstance(experiment[key], list) else [experiment[key]] for key in subparam_keys)
                    for subparam_combination in product(*subparam_values):
                        subparams = dict(zip(subparam_keys, subparam_combination))
                        glob_params = {}
                        glob_params["link_speed_Gbps"] = link_speed
                        glob_params["oversubscription_ratio"] = os_ratio
                        glob_params["topology_sizes"] = topology_size
                        glob_params["cc_algo"] = cc_algo
                        run_experiment(experiment['name'], glob_params, subparams)
                anlysis_and_plotting.plot_runtimes(directory)

def print_experiments(experiments, global_combinations, global_parameters):
    print("\nExperiments:")
    for experiment in experiments:
        print(f"Experiment Name: {experiment['name']}")
        handle_experiment(experiment, global_combinations, global_parameters)
        

def main():
    parser = argparse.ArgumentParser(description='Read and parse a JSON file containing experiments.')
    parser.add_argument('--json_file', required=True, help='Path to the JSON file')

    args = parser.parse_args()

    # Read and parse the JSON file
    data = read_json_file(args.json_file)

    # Experiments Folder
    if not os.path.exists(exp_directory):
        os.makedirs(exp_directory)
    
    # Print global parameters
    global_parameters = data['global_parameters']
    print("Global Parameters:")
    for key, value in global_parameters.items():
        print(f"  {key.replace('_', ' ').capitalize()}: {value}")

    # Get all global parameter combinations
    global_combinations = get_global_combinations(global_parameters)
    
    # Print experiments and handle each experiment specifically
    print_experiments(data['experiments'], global_combinations, global_parameters)

if __name__ == "__main__":
    main()
