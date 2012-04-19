#if defined(READ_EXISTING_GIT_REPOSITORY)

int ConvertRepository::load_revmap()
{
  int errors = 0;

  for (Git::Repository::commit_iterator
         i = repository->commits_begin();
       i != repository->commits_end();
       ++i) {
    std::string            message = (*i)->get_message();
    std::string::size_type offset  = message.find("SVN-Revision: ");
    if (offset == std::string::npos)
      throw std::logic_error("Cannot work with a repository"
                             " not created by subconvert");
    offset += 14;

    assert((*i)->get_oid());
#if defined(READ_REPOSITORY_FROM_DISK)
    branch->rev_map.insert
      (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                               (*i)->get_oid()));
#elif defined(READ_REPOSITORY_FROM_MAP_FILE)
    branch->rev_map.insert
      (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                               repository->read_commit((*i)->get_oid())));
#endif
  }
  return errors;
}

#endif // READ_EXISTING_GIT_REPOSITORY
