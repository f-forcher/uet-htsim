import json
import argparse
from itertools import product
import subprocess
import os
import shutil
import anlysis_and_plotting

# Some Global Variables
exp_directory = "experiments"

def check_if_supported_os_ratio(os_ratio):
    if os_ratio not in ["1:1", "4:1", "8:1"]:
        print(f"Error: Oversubscription ratio {os_ratio} is not supported. Supported values are: 1:1, 1:4, 1:8")
        exit(1)

def check_if_supported_topoogy_size(topology_size):
    if topology_size not in [128, 1024, 8192]:
        print(f"Error: Topology size {topology_size} is not supported. Supported values are: 128, 1024, 8192")
        exit(1)

def get_incast_outcast_ratio(ratio):
    return ratio.split(':')[0], ratio.split(':')[1]

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
    if (parameters_experiment["cc_algo"] == "rccc"):
        return ""
    elif (parameters_experiment["cc_algo"] == "nscc"):
        return "-sender_cc_only"
    elif (parameters_experiment["cc_algo"] == "nscc+rccc"):
        return "-sender_cc"
    elif (parameters_experiment["cc_algo"] == "rccc+os_cc"):
        return ""
    else:
        # Return error and exit the program
        print("Error: Invalid CC Algorithm, supported values are rccc, nscc, nscc+rccc and rccc+os_cc")
        exit(1)

def get_topology_file(topology_size, os_ratio):
    os_ratio = os_ratio[0]  

    if (topology_size == 128):
        return f"../topologies/fat_tree_128_{os_ratio}os.topo"
    elif (topology_size == 1024):
        return f"../topologies/fat_tree_1024_{os_ratio}os.topo"
    elif (topology_size == 8192):
        return f"../topologies/fat_tree_8192_{os_ratio}os.topo"
    
def update_link_speed_topo_file(topo_file, link_speed):
    with open(topo_file, 'r') as file:
        lines = file.readlines()
    
    with open(topo_file, 'w') as file:
        for line in lines:
            if 'Downlink_speed_Gbps' in line:
                parts = line.split()
                # Replace the last part (the speed) with the new speed
                parts[-1] = str(link_speed.replace("Gbps",""))
                line = ' '.join(parts) + '\n'
            file.write(line)

def get_file_to_run(name_exp, parameters_experiment, global_params):
    dir = f"{name_exp}_size{global_params['topology_sizes']}_osratio{global_params['oversubscription_ratio']}_linkspeed{global_params['link_speed_Gbps']}/tmp"
    if (name_exp == "incast"):
        cm_name = f"{exp_directory}/{dir}/incast_{parameters_experiment['ratio']}to1_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = (f"{exp_directory}/{dir}/incast_{parameters_experiment['ratio']}to1_size{parameters_experiment['message_size_bytes']}B_")
        cmd_to_run_cm_file = "python ../connection_matrices/gen_incast.py {} {} {} {} 0 42 1".format(cm_name, global_params["topology_sizes"], parameters_experiment["ratio"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "permutation"):
        cm_name = f"{exp_directory}/{dir}/permutation_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = f"{exp_directory}/{dir}/permutation_size{parameters_experiment['message_size_bytes']}B_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_permutation.py {} {} {} {} 0 42".format(cm_name, global_params["topology_sizes"], global_params["topology_sizes"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "outcast_incast"):
        
        incast_ratio, outcast_ratio = get_incast_outcast_ratio(parameters_experiment['ratio']) 
        cm_name = f"{exp_directory}/{dir}/outcast_size{parameters_experiment['message_size_bytes']}B_incast{incast_ratio}_outcast{outcast_ratio}.cm"
        output_file = f"{exp_directory}/{dir}/outcast_size{parameters_experiment['message_size_bytes']}B_incast{incast_ratio}_outcast{outcast_ratio}_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_outcast_incast.py {} {} {} {} {} 42".format(cm_name, global_params["topology_sizes"], incast_ratio, outcast_ratio, parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "all_reduce_ring"):
        
        cm_name = f"{exp_directory}/{dir}/allreduce_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = f"{exp_directory}/{dir}/allreduce_size{parameters_experiment['message_size_bytes']}B_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_allreduce.py {} {} {} {} {} 1 42".format(cm_name, global_params["topology_sizes"], global_params["topology_sizes"], global_params["topology_sizes"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "all_reduce_butterfly"):
        
        cm_name = f"{exp_directory}/{dir}/allreduceButterfly_size{parameters_experiment['message_size_bytes']}B.cm"
        output_file = f"{exp_directory}/{dir}/allreduceButterfly_size{parameters_experiment['message_size_bytes']}B_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_allreduce_butterfly.py {} {} {} {} {} 1 42".format(cm_name, global_params["topology_sizes"], 1, global_params["topology_sizes"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
            subprocess.run(cmd_to_run_cm_file, shell=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"An error occurred while running the command: {e}")
        return cm_name, output_file
    
    elif (name_exp == "all_to_all_windowed"):
        
        cm_name = f"{exp_directory}/{dir}/alltoallwindowed_size{parameters_experiment['message_size_bytes']}B_window{parameters_experiment['parallel_connections']}.cm"
        output_file = f"{exp_directory}/{dir}/alltoallwindowed_size{parameters_experiment['message_size_bytes']}B__window{parameters_experiment['parallel_connections']}_"
        cmd_to_run_cm_file = "python ../connection_matrices/gen_serialn_alltoall.py {} {} {} {} {} {} 0 42".format(cm_name, global_params["topology_sizes"], global_params["topology_sizes"], global_params["topology_sizes"], parameters_experiment["parallel_connections"], parameters_experiment["message_size_bytes"])
        try:
            # Execute the command
            print(f"Creating CM named {cmd_to_run_cm_file}")
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
    update_link_speed_topo_file(topo_file, global_params["link_speed_Gbps"])
    # Define the static command to execute
    cc_algo_to_use = get_cc_name(global_params)
    disable_os_cc = ""
    if (global_params["cc_algo"] == "rccc"):
        disable_os_cc = "-forse_disable_oversubscribed_cc"
    command = "../htsim_uec -tm {} -end 1000000 {} -topo {} -linkspeed {} {} > {}".format(connection_matrix, cc_algo_to_use, topo_file, int(global_params["link_speed_Gbps"].replace("Gbps","")) * 1000, disable_os_cc, output_file)
    print(f"Executing: {command}")
    try:
        # Execute the command
        subprocess.run(command, shell=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while running the command: {e}")

def handle_experiment(experiment, global_combinations, global_params, args):

    for link_speed in global_params["link_speed_Gbps"]:
        for os_ratio in global_params["oversubscription_ratio"]:
            check_if_supported_os_ratio(os_ratio)
            for topology_size in global_params["topology_sizes"]:
                check_if_supported_topoogy_size(topology_size)
                directory = os.path.join(exp_directory, f"{experiment['name']}_size{topology_size}_osratio{os_ratio}_linkspeed{link_speed}")
                if not os.path.exists(directory):
                    os.makedirs(directory)
                delete_folder_contents(directory)
                directory_tmp = os.path.join(exp_directory, f"{experiment['name']}_size{topology_size}_osratio{os_ratio}_linkspeed{link_speed}")
                directory_tmp = os.path.join(directory_tmp,"tmp")
                if not os.path.exists(directory_tmp):
                    os.makedirs(directory_tmp)
                delete_folder_contents(directory_tmp)
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
                anlysis_and_plotting.plot_runtimes(directory_tmp, directory, args)

def launch_experiments(experiments, global_combinations, global_parameters, args):
    print("\nExperiments:")
    for experiment in experiments:
        print(f"Experiment Name: {experiment['name']}")
        handle_experiment(experiment, global_combinations, global_parameters, args)
        

def main():
    parser = argparse.ArgumentParser(description='Read and parse a JSON file containing experiments.')
    parser.add_argument('--config_json_file', required=True, help='Path to the JSON file')
    parser.add_argument('--show_plot', action='store_true', help='A boolean flag')

    args = parser.parse_args()

    # Read and parse the JSON file
    data = read_json_file(args.config_json_file)

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
    launch_experiments(data['experiments'], global_combinations, global_parameters, args)

if __name__ == "__main__":
    main()
