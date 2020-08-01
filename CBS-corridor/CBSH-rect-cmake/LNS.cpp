#include "LNS.h"




bool LNS::run(float _hard_time_limit, float _soft_time_limit)
{
    start_time = Time::now();
    hard_time_limit = _hard_time_limit;
    soft_time_limit = min(_soft_time_limit, hard_time_limit);
    if (!getInitialSolution()) // get initial solution
        return false;

    size_t solution_cost = 0;
    int sum_of_showup_time = 0;
    size_t makespan = 0;
    for (const auto& path : al.paths_all)
    {
        solution_cost += path.size() - 1;
        makespan = max(path.size() - 1, makespan);
        for (int t  = 0; t < (int)path.size(); t++)
        {
            if (path[t].location >= 0)
            {
                sum_of_showup_time += t;
                break;
            }
        }
    }
    runtime = ((fsec)(Time::now() - start_time)).count();
    //if (options1.debug)
    cout << "Initial solution cost = " << solution_cost << ", "
         << "travel time = " << solution_cost - sum_of_showup_time << ", "
         << "makespan = " << makespan << ", "
         << "runtime = " << runtime << endl;
    iteration_stats.emplace_back(al.agents_all.size(), 0,
                                 runtime, runtime,
                                 makespan,
                                 solution_cost,
                                 destroy_strategy,
                                 (double)(solution_cost) / max_timestep / al.agents_all.size(),
                                 0,
                                 0,
                                 0);
    if (destroy_strategy == 3)
    {
        adaptive_destroy = true;
        destroy_heuristics.assign(3, 1);
    }
    else
        adaptive_destroy = false;
    boost::unordered_set<int> tabu_list;
    bool succ;
    auto old_runtime = runtime;
    while (runtime < soft_time_limit && iteration_stats.size() < 10000)
    {
        runtime =((fsec)(Time::now() - start_time)).count();
        if (adaptive_destroy)
        {
            double sum = 0;
            for (const auto& h : destroy_heuristics)
                sum += h;
            cout << "destroy heuristics = ";
            for (const auto& h : destroy_heuristics)
                cout << h / sum << ",";
            double r = (double) rand() / RAND_MAX;
            if (r * sum < destroy_heuristics[0])
                destroy_strategy = 0;
            else if (r * sum < destroy_heuristics[0] + destroy_heuristics[1])
                destroy_strategy = 1;
            else
                destroy_strategy = 2;
            cout << "Choose destroy strategy " << destroy_strategy << endl;
        }
        switch (destroy_strategy)
        {
            case 0:
                generateNeighborByRandomWalk(tabu_list);
                break;
            case 1:
                succ = generateNeighborByStart();
                if(!succ) // no two agents have the same start locations
                    return true;
                break;
            case 2:
                succ = generateNeighborByIntersection();
                if(!succ) // the selected intersection has fewer than 2 agents
                    continue;
                break;
            default:
                cout << "Wrong neighbor generation strategy" << endl;
                exit(0);
        }

        switch(replan_strategy)
        {
            case 0: // CBS
                if (neighbors.size() > group_size) // resize the group
                {
                    sortNeighborsRandomly();
                    neighbors.resize(group_size);
                }
                succ = replanByCBS();
                if (succ)
                    group_size++;
                else if (group_size > 1)
                    group_size--;
                break;
            case 1: // prioritized planning
                switch (prirority_ordering_strategy)  // generate priority ordering for prioritized planning
                {
                    case 0:
                        sortNeighborsRandomly();
                        break;
                    case 1:
                        sortNeighborsByRegrets();
                        break;
                    default:
                        cout << "Wrong prirority ordering strategy" << endl;
                        exit(0);
                }
                replanByPP();
                break;
            default:
                cout << "Wrong replanning strategy" << endl;
                exit(0);
        }

        if (adaptive_destroy) // update destroy heuristics
        {
            if (delta_costs < 0)
                destroy_heuristics[destroy_strategy] = reaction_factor * (-delta_costs)
                                                   + (1 - reaction_factor) * destroy_heuristics[destroy_strategy];
            else
                destroy_heuristics[destroy_strategy] = (1 - decay_factor) * destroy_heuristics[destroy_strategy];
        }
        runtime = ((fsec)(Time::now() - start_time)).count();
        solution_cost += delta_costs;
        // if (options1.debug)
            cout << "Iteration " << iteration_stats.size() << ", "
             << "group size = " << neighbors.size() << ", "
             << "solution cost = " << solution_cost << ", "
             << "remaining time = " << soft_time_limit - runtime << endl;
        iteration_stats.emplace_back(neighbors.size(), 0,
                                     runtime, runtime - old_runtime,
                                     makespan,
                                     solution_cost,
                                     destroy_strategy,
                                     (double)(solution_cost) / max_timestep / al.agents_all.size(),
                                     0,
                                     0,
                                     0);
        old_runtime = runtime;
        if (replan_strategy == 0 && group_size > al.agents_all.size())
            return true; // CBS has replanned paths for all agents. No need for further iterations
    }
    return true;
}

bool LNS::getInitialSolution()
{
    if (options1.debug)
        cout << "Prioritized planning" << endl;
    int screen;
    screen = options1.debug;
    if (options1.debug)
        cout << "Sort the agents" << endl;
    neighbors.resize(al.agents_all.size());
    for (int i = 0; i < (int)al.agents_all.size(); i++)
        neighbors[i] = i;
    sortNeighborsByStrategy();

    al.num_of_agents = 1;
    al.agents.resize(1);
    int remaining_agents = (int)neighbors.size();
    for (auto agent : neighbors)
    {
        runtime = ((fsec)(Time::now() - start_time)).count();
        if (runtime >= hard_time_limit)
        {
            return false;
        }
        al.agents[0] = &al.agents_all[agent];
        if (options1.debug)
            cout << "Remaining agents = " << remaining_agents <<
             ", remaining time = " << hard_time_limit - runtime << " seconds. " << endl
                    << "Agent " << al.agents[0]->agent_id << endl;
        MultiMapICBSSearch<FlatlandLoader> icbs(&ml, &al, f_w, c, 0, options1.debug? 3 : 0, options1);
        icbs.runICBSSearch();
        updateCBSResults(icbs);
        addAgentPath(agent, *icbs.paths[0]);
        remaining_agents--;
    }

    runtime = ((fsec)(Time::now() - start_time)).count();
    if (options1.debug)
        cout << endl << endl << "Find a solution for " << al.getNumOfAllAgents() - al.getNumOfUnplannedAgents()
         << " agents (including " << al.getNumOfDeadAgents() << " dead agents) in " << runtime << " seconds!" << endl;

    return true;
}

bool LNS::generateNeighborByStart()
{
    if (start_locations.empty())
    {
        for (int i = 0; i < (int)al.agents_all.size(); i++)
        {
            auto start = ml.linearize_coordinate(al.agents_all[i].initial_location);
            start_locations[start].push_back(i);
        }
        auto it = start_locations.begin();
        while(it != start_locations.end()) // delete start locations that have only one agent
        {
            if (it->second.size() == 1)
                it = start_locations.erase(it);
            else
                ++it;
        }
    }
    if (start_locations.empty())
        return false;
    auto step = rand() % start_locations.size();
    auto it = start_locations.begin();
    advance(it, step);
    neighbors.assign(it->second.begin(), it->second.end());
    if (neighbors.size() > max_group_size ||
        (replan_strategy == 0 && neighbors.size() > group_size)) // resize the group for CBS
    {
        sortNeighborsRandomly();
        neighbors.resize(group_size);
    }
    if (options1.debug)
        cout << "Generate " << neighbors.size() << " neighbors by start location " << it->first << endl;
    return true;
}

bool LNS::generateNeighborByIntersection()
{
    if (intersections.empty())
    {
        for (int i = 0; i < ml.map_size(); i++)
        {
            if (ml.getDegree(i) > 2)
                intersections.push_back(i);
        }
    }

    set<int> neighbors_set;
    int location = intersections[rand() % intersections.size()];
    al.constraintTable.get_agents(neighbors_set, location);
    if (neighbors_set.size() <= 1)
        return false;
    neighbors.assign(neighbors_set.begin(), neighbors_set.end());
    if (replan_strategy == 0 && neighbors.size() > group_size) // resize the group for CBS
    {
        sortNeighborsRandomly();
        neighbors.resize(group_size);
    }
    if (options1.debug)
        cout << "Generate " << neighbors.size() << " neighbors by intersection " << location << endl;
    return true;
}

void LNS::generateNeighborByRandomWalk(boost::unordered_set<int>& tabu_list)
{
    if (group_size >= (int)al.paths_all.size())
    {
        neighbors.resize(al.paths_all.size());
        for (int i = 0; i < (int)al.paths_all.size(); i++)
            neighbors[i] = i;
        return;
    }

    // find the agent with max regret
    int a = -1;
    for (int i = 0; i < al.paths_all.size(); i++)
    {
        if (tabu_list.find(i) != tabu_list.end())
            continue;
        if (a < 0 || compareByRegrets(i, a))
        {
            a = i;
        }
    }
    if (tabu_list.size() > al.paths_all.size() / 2)
        tabu_list.clear();
    else
        tabu_list.insert(a);

    set<int> neighbors_set;
    neighbors_set.insert(a);
    int T = al.paths_all[a].size();
    int count = 0;
    while (neighbors_set.size() < group_size && count < 10 && T > 0)
    {
        int t = rand() % T;
        randomWalk(a, al.paths_all[a][t], t, neighbors_set, group_size, (int) al.paths_all[a].size() - 1);
        T = t;
        count++;
    }
    while (neighbors_set.size() < group_size)
    {
        int new_agent = rand() % al.paths_all.size();
        neighbors_set.insert(new_agent);
    }

    neighbors.assign(neighbors_set.begin(), neighbors_set.end());
    if (options1.debug)
        cout << "Generate " << neighbors.size() << " neighbors by random walks of agent " << a
        << "(" << al.agents_all[a].distance_to_goal << "->" << al.paths_all[a].size() - 1 << ")" << endl;
}


void LNS::replanByPP()
{
    updateNeighborPaths();
    deleteNeighborPaths();
    al.num_of_agents = 1;
    al.agents.resize(1);
    list<Path> new_paths;
    int sum_of_costs = 0;
    int sum_of_showup_time = 0;
    int makespan = 0;
    for (const auto& agent : neighbors)
    {
        runtime = ((fsec)(Time::now() - start_time)).count();
        if (runtime >= soft_time_limit)
        { // change back to the original paths
            auto path = neighbor_paths.begin();
            for (const auto& agent : neighbors)
            {
                al.paths_all[agent] = *path;
                ++path;
            }
            return;
        }
        al.agents[0] = &al.agents_all[agent];
        MultiMapICBSSearch<FlatlandLoader> icbs(&ml, &al, f_w, c, 0, options1.debug? 3 : 0, options1);
        icbs.runICBSSearch();
        updateCBSResults(icbs);
        assert(icbs.paths[0]->back().location == al.paths_all[agent].back().location);
        addAgentPath(agent, *icbs.paths[0]);
        if (icbs.paths[0]->empty())
        {
            sum_of_costs += max_timestep;
            makespan = max_timestep;
        }
        else
        {
            sum_of_costs += (int)icbs.paths[0]->size() - 1;
            makespan = max(makespan, (int)icbs.paths[0]->size() - 1);
            for (int t  = 0; t < (int)icbs.paths[0]->size(); t++)
            {
                if (icbs.paths[0]->at(t).location >= 0)
                {
                    sum_of_showup_time += t;
                    break;
                }
            }
        }
    }
    if (sum_of_costs < neighbor_sum_of_costs ||
        (sum_of_costs == neighbor_sum_of_costs && sum_of_showup_time > neighbor_sum_of_showup_time) ||
        (sum_of_costs == neighbor_sum_of_costs && sum_of_showup_time == neighbor_sum_of_showup_time && makespan < neighbor_makespan))
    {
        delta_costs = sum_of_costs - neighbor_sum_of_costs;
    }
    else
    { // change back to the original paths
        deleteNeighborPaths();
        auto path = neighbor_paths.begin();
        for (auto agent : neighbors)
        {
            addAgentPath(agent, *path);
            ++path;
        }
        delta_costs = 0;
    }
}

bool LNS::replanByCBS()
{
    updateNeighborPathsCosts();
    deleteNeighborPaths();
    al.num_of_agents = (int)neighbors.size();
    al.agents.clear();
    for (auto i : neighbors)
    {
        al.agents.push_back(&al.agents_all[i]);
    }
    runtime = ((fsec)(Time::now() - start_time)).count();
    double cbs_time_limit = min((double)soft_time_limit - runtime, 10.0);
    MultiMapICBSSearch<FlatlandLoader> icbs(&ml, &al, f_w, c, cbs_time_limit, options1.debug? 3 : 0, options1);
    icbs.trainCorridor1 = trainCorridor1;
    icbs.corridor2 = corridor2;
    icbs.chasing_reasoning = chasing;
    if (options1.debug)
        cout << "start search engine" << endl;
    bool res = icbs.runICBSSearch();
    updateCBSResults(icbs);
    // assert(!res || icbs.solution_cost <= neighbor_sum_of_costs);
    if (res && icbs.solution_cost <= neighbor_sum_of_costs)
    {
        int i = 0;
        for (auto n : neighbors)
        {
            assert(icbs.paths[i]->back().location == al.paths_all[n].back().location);
            addAgentPath(n, *icbs.paths[i]);
            i++;
        }
        delta_costs = icbs.solution_cost - neighbor_sum_of_costs;
    }
    else
    {
        for (auto i : neighbors)
        {
            if(!al.constraintTable.insert_path(i, al.paths_all[i]))
                exit(13);
        }
        delta_costs = 0;
    }
    return res;
}


void LNS::updateNeighborPaths()
{
    if (options1.debug)
        cout << "Agents ids: ";
    neighbor_sum_of_costs = 0;
    neighbor_sum_of_showup_time = 0;
    neighbor_makespan = 0;
    neighbor_paths.clear();
    for (auto i : neighbors)
    {
        if (options1.debug)
            cout << i << ",";
        neighbor_paths.emplace_back(al.paths_all[i]);
        if (al.paths_all[i].empty())
        {
            neighbor_sum_of_costs += max_timestep;
            neighbor_makespan = max_timestep;
        }
        else
        {
            neighbor_sum_of_costs += (int)al.paths_all[i].size() - 1;
            neighbor_makespan = max(neighbor_makespan, (int)al.paths_all[i].size() - 1);
            for (int t  = 0; t < (int)al.paths_all[i].size(); t++)
            {
                if (al.paths_all[i][t].location >= 0)
                {
                    neighbor_sum_of_showup_time += t;
                    break;
                }
            }
        }
    }
    if (options1.debug)
        cout << endl;
}

void LNS::updateNeighborPathsCosts()
{
    if (options1.debug)
        cout << "Agents ids: ";
    neighbor_sum_of_costs = 0;
    neighbor_makespan = 0;
    neighbor_paths.clear();
    for (auto i : neighbors)
    {
        if (options1.debug)
            cout << i << ",";
        neighbor_sum_of_costs += (int)al.paths_all[i].size() - 1;
        neighbor_makespan = max(neighbor_makespan, (int)al.paths_all[i].size() - 1);
    }
    if (options1.debug)
        cout << endl;
}

void LNS::addAgentPath(int agent, const Path& path)
{
    assert(agent == al.agents_all[agent].agent_id);
    if(!al.constraintTable.insert_path(agent, path))
        exit(10);
    al.paths_all[agent] = path;
}

void LNS::deleteNeighborPaths()
{
    for (auto i : neighbors)
    {
        assert(i == al.agents_all[i].agent_id);
        al.constraintTable.delete_path(i, al.paths_all[i]);
    }
}

void LNS::sortNeighborsRandomly()
{
    std::random_shuffle(neighbors.begin(), neighbors.end());
    if (options1.debug) {
        for (auto agent : neighbors) {
            cout << agent << "(" << al.agents_all[agent].distance_to_goal << "->" << al.paths_all[agent].size() - 1
                 << "), ";
        }
        cout << endl;
    }
}

void LNS::sortNeighborsByRegrets()
{
    quickSort(neighbors, 0, neighbors.size() - 1, true);
    if (options1.debug) {
        for (auto agent : neighbors) {
            cout << agent << "(" << al.agents_all[agent].distance_to_goal << "->" << al.paths_all[agent].size() - 1
                 << "), ";
        }
        cout << endl;
    }
}

void LNS::sortNeighborsByStrategy()
{
    if (agent_priority_strategy == 5)
    {
        // decide the agent priority for agents at the same start location
        start_locations.clear(); // map the agents to their start locations
        for (auto i : neighbors)
            start_locations[ml.linearize_coordinate(al.agents_all[i].initial_location)].push_back(i);
        for (auto& agents : start_locations)
        {
            vector<int> agents_vec(agents.second.begin(), agents.second.end());
            quickSort(agents_vec, 0, agents_vec.size() - 1, false);
            for (int i = 0; i < (int)agents.second.size(); i++)
            {
                al.agents_all[agents_vec[i]].priority = i;
            }
        }
    }

    // sort the agents
    if (agent_priority_strategy != 0)
        quickSort(neighbors, 0, (int)neighbors.size() - 1, false);
}


void LNS::quickSort(vector<int>& agent_order, int low, int high, bool regret)
{
    if (low >= high)
        return;
    int pivot = agent_order[high];    // pivot
    int i = low;  // Index of smaller element
    for (int j = low; j <= high - 1; j++)
    {
        // If current element is smaller than or equal to pivot
        if ((regret && compareByRegrets(agent_order[j], pivot)) ||
            al.compareAgent(al.agents_all[agent_order[j]], al.agents_all[pivot], agent_priority_strategy))
        {
            std::swap(agent_order[i], agent_order[j]);
            i++;    // increment index of smaller element
        }
    }
    std::swap(agent_order[i], agent_order[high]);

    quickSort(agent_order, low, i - 1, regret);  // Before i
    quickSort(agent_order, i + 1, high, regret); // After i
}

void LNS::randomWalk(int agent_id, const PathEntry& start, int start_timestep,
        set<int>& conflicting_agents, int neighbor_size, int upperbound)
{
    // a random walk with path that is shorter than upperbound and has conflicting with neighbor_size agents
    int speed = al.agents_all[agent_id].speed;
    const auto& heuristics = al.agents_all[agent_id].heuristics;
    int loc = start.location;
    int heading = start.heading;
    auto position_fraction = start.position_fraction;
    auto exit_heading = start.exit_heading;
    int exit_loc = start.exit_loc;
    int h_val = heuristics[loc].get_hval(heading) / speed;
    if (exit_loc >= 0 && speed < 1)
    {
        int h1 = heuristics[loc].get_hval(heading);
        int h2 = heuristics[exit_loc].get_hval(exit_heading);
        h_val = h1 / speed - (h2 - h1)*position_fraction;
    }

    for (int t = start_timestep; t < upperbound; t++)
    {
        list<Transition> transitions;
        if(loc == -1){
            Transition move;
            move.location = loc;
            move.heading = heading;
            move.position_fraction = position_fraction;
            move.exit_loc = exit_loc;
            move.exit_heading = exit_heading;
            transitions.push_back(move);

            Transition move2;
            move2.location = ml.linearize_coordinate(al.agents_all[agent_id].position);
            move2.heading = heading;
            move2.position_fraction = position_fraction;
            move2.exit_loc = exit_loc;
            move2.exit_heading = exit_heading;
            transitions.push_back(move2);
        }
        else if (position_fraction + speed >= 0.97)
        {
            if (position_fraction == 0)
            {
                ml.get_transitions(transitions, loc, heading, false);
                assert(!transitions.empty());
            }
            else {
                Transition move;
                move.location = exit_loc;
                move.heading = exit_heading;
                move.position_fraction = 0;
                transitions.push_back(move);
            }
        }
        else if (position_fraction == 0)
        {
            ml.get_exits(transitions, loc, heading, speed, false);
            assert(!transitions.empty());
        }
        else { //<0.97 and po_frac not 0
            Transition move2;
            move2.location = loc;
            move2.heading = heading;
            move2.position_fraction = position_fraction + al.agents[agent_id]->speed;
            move2.exit_loc = exit_loc;
            move2.exit_heading = exit_heading;
            transitions.push_back(move2);
        }

        while(!transitions.empty())
        {
            int step = rand() % transitions.size();
            auto it = transitions.begin();
            advance(it, step);
            int next_h_val;
            if (it->location == -1)
                next_h_val = h_val;
            else if (exit_loc >= 0 && speed < 1)
            {
                int h1 = heuristics[it->location].get_hval(it->heading);
                int h2 = heuristics[it->exit_loc].get_hval(it->exit_heading);
                next_h_val = h1 / speed - (h2 - h1) * (it->position_fraction / speed);
            }
            else
                next_h_val = heuristics[it->location].get_hval(it->heading) / speed;

            if (t + 1 + next_h_val < upperbound) // move to this location
            {
                loc = it->location;
                heading = it->heading;
                position_fraction = it->position_fraction;
                exit_heading = it->exit_heading;
                exit_loc = it->exit_loc;
                h_val = next_h_val;
                assert(agent_id == al.agents_all[agent_id].agent_id);
                al.constraintTable.get_conflicting_agents(agent_id, conflicting_agents, loc, t + 1);
                break;
            }
            transitions.erase(it);
        }
        if (transitions.empty() || conflicting_agents.size() >= neighbor_size || h_val == 0)
            break;
    }
}